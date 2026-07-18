#include "zola/video_source.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "zola/audio_output.hpp"
#include "zola/logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace zola {
namespace {

std::uint8_t luminance_u8(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  // Rec. 709 (same as ImageSource)
  const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  return static_cast<std::uint8_t>(std::lround(std::clamp(y, 0.0, 255.0)));
}

} // namespace

struct VideoSource::Impl {
  std::filesystem::path path;
  AVFormatContext* fmt = nullptr;
  AVCodecContext* video_codec = nullptr;
  const AVCodec* video_decoder = nullptr;
  AVCodecContext* audio_codec = nullptr;
  const AVCodec* audio_decoder = nullptr;
  SwsContext* sws = nullptr;
  SwrContext* swr = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* rgb = nullptr; // swscale dest: RGB24
  AVFrame* audio_frame = nullptr;
  AVPacket* packet = nullptr;
  int video_stream = -1;
  int audio_stream = -1;
  std::size_t width = 0;
  std::size_t height = 0;
  double fps = 0.0;
  AudioFormat audio_format{};
  bool opened = false;
  bool eof = false;
  bool audio_eof = false;

  // Reusable planar float buffer for swr output staging.
  std::vector<std::uint8_t> swr_buffer;

  void close() noexcept {
    if (sws) {
      sws_freeContext(sws);
      sws = nullptr;
    }
    if (swr) {
      swr_free(&swr);
    }
    if (rgb) {
      av_frame_free(&rgb);
    }
    if (frame) {
      av_frame_free(&frame);
    }
    if (audio_frame) {
      av_frame_free(&audio_frame);
    }
    if (packet) {
      av_packet_free(&packet);
    }
    if (video_codec) {
      avcodec_free_context(&video_codec);
    }
    if (audio_codec) {
      avcodec_free_context(&audio_codec);
    }
    if (fmt) {
      avformat_close_input(&fmt);
    }
    opened = false;
  }

  ~Impl() { close(); }
};

VideoSource::VideoSource(std::filesystem::path path)
    : impl_(std::make_unique<Impl>()) {
  impl_->path = std::move(path);
}

VideoSource::~VideoSource() = default;

VideoSource::VideoSource(VideoSource&&) noexcept = default;
VideoSource& VideoSource::operator=(VideoSource&&) noexcept = default;

std::size_t VideoSource::width() const noexcept {
  return impl_ ? impl_->width : 0;
}

std::size_t VideoSource::height() const noexcept {
  return impl_ ? impl_->height : 0;
}

double VideoSource::fps() const noexcept {
  return impl_ ? impl_->fps : 0.0;
}

bool VideoSource::has_audio() const noexcept {
  return impl_ && impl_->audio_stream >= 0;
}

AudioFormat VideoSource::audio_format() const noexcept {
  return impl_ ? impl_->audio_format : AudioFormat{};
}

VoidResult VideoSource::open() {
  if (!impl_) {
    return std::unexpected(Error(ErrorKind::invalid_argument));
  }
  if (impl_->opened) {
    return {};
  }

  const std::string path = impl_->path.string();
  int rc;
  rc = avformat_open_input(&impl_->fmt, path.c_str(), nullptr, nullptr);
  if (rc < 0) {
    Error e(ErrorKind::ffmpeg_error);
    e.ffmpeg_code = rc;
    char errbuf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(rc, errbuf, sizeof(errbuf));
    e.message = errbuf;
    Logger::error(to_string(e));
    return std::unexpected(e);
  }
  rc = avformat_find_stream_info(impl_->fmt, nullptr);
  if (rc < 0) {
    impl_->close();
    Error e(ErrorKind::ffmpeg_error);
    e.ffmpeg_code = rc;
    char errbuf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(rc, errbuf, sizeof(errbuf));
    e.message = errbuf;
    Logger::error(to_string(e));
    return std::unexpected(e);
  }

  impl_->video_stream = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1,
                                            -1, &impl_->video_decoder, 0);
  if (impl_->video_stream < 0 || !impl_->video_decoder) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::unsupported));
  }

  AVStream* stream = impl_->fmt->streams[impl_->video_stream];
  impl_->video_codec = avcodec_alloc_context3(impl_->video_decoder);
  if (!impl_->video_codec) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }
  if (avcodec_parameters_to_context(impl_->video_codec, stream->codecpar) < 0) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }
  if (avcodec_open2(impl_->video_codec, impl_->video_decoder, nullptr) < 0) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }

  impl_->width = static_cast<std::size_t>(impl_->video_codec->width);
  impl_->height = static_cast<std::size_t>(impl_->video_codec->height);
  if (impl_->width == 0 || impl_->height == 0) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }

  // Prefer average frame rate; fall back to r_frame_rate.
  AVRational rate = stream->avg_frame_rate;
  if (rate.num <= 0 || rate.den <= 0) {
    rate = stream->r_frame_rate;
  }
  if (rate.num > 0 && rate.den > 0) {
    impl_->fps = av_q2d(rate);
  } else {
    impl_->fps = 24.0;
  }

  impl_->frame = av_frame_alloc();
  impl_->rgb = av_frame_alloc();
  impl_->packet = av_packet_alloc();
  if (!impl_->frame || !impl_->rgb || !impl_->packet) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }

  // Destination RGB24 buffer owned by rgb frame (preallocated once).
  impl_->rgb->format = AV_PIX_FMT_RGB24;
  impl_->rgb->width = impl_->video_codec->width;
  impl_->rgb->height = impl_->video_codec->height;
  if (av_frame_get_buffer(impl_->rgb, 32) < 0) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }

  impl_->sws =
      sws_getContext(impl_->video_codec->width, impl_->video_codec->height,
                     impl_->video_codec->pix_fmt, impl_->video_codec->width,
                     impl_->video_codec->height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                     nullptr, nullptr, nullptr);
  if (!impl_->sws) {
    impl_->close();
    return std::unexpected(Error(ErrorKind::decode_failure));
  }

  // Optional audio stream. Failure here is not fatal — video still plays.
  impl_->audio_stream = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_AUDIO, -1,
                                             -1, &impl_->audio_decoder, 0);
  if (impl_->audio_stream >= 0 && impl_->audio_decoder) {
    AVStream* astream = impl_->fmt->streams[impl_->audio_stream];
    impl_->audio_codec = avcodec_alloc_context3(impl_->audio_decoder);
    if (impl_->audio_codec &&
        avcodec_parameters_to_context(impl_->audio_codec, astream->codecpar) >= 0 &&
        avcodec_open2(impl_->audio_codec, impl_->audio_decoder, nullptr) >= 0) {
      // Target: S16 interleaved stereo at 48 kHz.
      constexpr int target_rate = 48000;
      constexpr int target_channels = 2;
      impl_->audio_format = AudioFormat{target_rate, target_channels};

      AVChannelLayout in_layout = impl_->audio_codec->ch_layout;
      if (in_layout.nb_channels == 0) {
        av_channel_layout_default(&in_layout, impl_->audio_codec->ch_layout.nb_channels > 0
                                                  ? impl_->audio_codec->ch_layout.nb_channels
                                                  : 2);
      }
      AVChannelLayout out_layout;
      av_channel_layout_default(&out_layout, target_channels);

      impl_->swr = swr_alloc();
      if (impl_->swr) {
        av_opt_set_int(impl_->swr, "in_channel_count", in_layout.nb_channels, 0);
        av_opt_set_int(impl_->swr, "out_channel_count", target_channels, 0);
        av_opt_set_int(impl_->swr, "in_sample_rate", impl_->audio_codec->sample_rate, 0);
        av_opt_set_int(impl_->swr, "out_sample_rate", target_rate, 0);
        av_opt_set_sample_fmt(impl_->swr, "in_sample_fmt", impl_->audio_codec->sample_fmt, 0);
        av_opt_set_sample_fmt(impl_->swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        av_opt_set_chlayout(impl_->swr, "in_chlayout", &in_layout, 0);
        av_opt_set_chlayout(impl_->swr, "out_chlayout", &out_layout, 0);
        if (swr_init(impl_->swr) < 0) {
          swr_free(&impl_->swr);
          impl_->audio_format = AudioFormat{};
          impl_->audio_stream = -1;
        }
      } else {
        impl_->audio_format = AudioFormat{};
        impl_->audio_stream = -1;
      }
      av_channel_layout_uninit(&in_layout);
      av_channel_layout_uninit(&out_layout);
    } else {
      if (impl_->audio_codec) {
        avcodec_free_context(&impl_->audio_codec);
        impl_->audio_codec = nullptr;
      }
      impl_->audio_stream = -1;
    }
    if (impl_->audio_stream >= 0) {
      impl_->audio_frame = av_frame_alloc();
      if (!impl_->audio_frame) {
        impl_->close();
        return std::unexpected(Error(ErrorKind::decode_failure));
      }
    }
  }

  impl_->opened = true;
  impl_->eof = false;
  impl_->audio_eof = false;
  return {};
}

Result<bool> VideoSource::next_frame(Frame& out) {
  if (!impl_ || !impl_->opened) {
    return std::unexpected(Error(ErrorKind::invalid_argument));
  }
  if (impl_->eof) {
    return std::unexpected(Error(ErrorKind::end_of_stream));
  }

  auto convert_to_out = [&](AVFrame* decoded, AVStream* vstream) -> VoidResult {
    if (av_frame_make_writable(impl_->rgb) < 0) {
      return std::unexpected(Error(ErrorKind::decode_failure));
    }
    sws_scale(impl_->sws, decoded->data, decoded->linesize, 0,
              impl_->video_codec->height, impl_->rgb->data, impl_->rgb->linesize);

    out.ensure_size(impl_->width, impl_->height);
    out.ensure_color();

    // Copy RGB (line-by-line for padding) and derive Rec.709 gray in one pass.
    const int src_linesize = impl_->rgb->linesize[0];
    const auto* src = impl_->rgb->data[0];
    auto* gray = out.data();
    auto* rgb_dst = out.rgb().data();
    for (std::size_t y = 0; y < impl_->height; ++y) {
      const auto* row =
          src + static_cast<std::size_t>(y) * static_cast<std::size_t>(src_linesize);
      auto* gray_row = gray + y * impl_->width;
      auto* rgb_row = rgb_dst + y * impl_->width * 3;
      for (std::size_t x = 0; x < impl_->width; ++x) {
        const std::uint8_t r = row[x * 3 + 0];
        const std::uint8_t g = row[x * 3 + 1];
        const std::uint8_t b = row[x * 3 + 2];
        rgb_row[x * 3 + 0] = r;
        rgb_row[x * 3 + 1] = g;
        rgb_row[x * 3 + 2] = b;
        gray_row[x] = luminance_u8(r, g, b);
      }
    }
    if (decoded->pts != AV_NOPTS_VALUE) {
      out.set_pts(static_cast<double>(decoded->pts) * av_q2d(vstream->time_base));
    }
    return {};
  };

  // Drain / read until we get one video frame.
  AVStream* vstream = impl_->fmt->streams[impl_->video_stream];
  for (;;) {
    const int rec = avcodec_receive_frame(impl_->video_codec, impl_->frame);
    if (rec == 0) {
      auto conv = convert_to_out(impl_->frame, vstream);
      av_frame_unref(impl_->frame);
      if (!conv) {
        return std::unexpected(conv.error());
      }
      return true;
    }
    if (rec != AVERROR(EAGAIN)) {
      if (rec == AVERROR_EOF) {
        impl_->eof = true;
        return std::unexpected(Error(ErrorKind::end_of_stream));
      }
      return std::unexpected(Error(ErrorKind::decode_failure));
    }

    // Need more input.
    const int rr = av_read_frame(impl_->fmt, impl_->packet);
    if (rr < 0) {
      // Flush decoder.
      avcodec_send_packet(impl_->video_codec, nullptr);
      const int rec2 = avcodec_receive_frame(impl_->video_codec, impl_->frame);
      if (rec2 == 0) {
        auto conv = convert_to_out(impl_->frame, vstream);
        av_frame_unref(impl_->frame);
        if (!conv) {
          return std::unexpected(conv.error());
        }
        return true;
      }
      impl_->eof = true;
      return std::unexpected(Error(ErrorKind::end_of_stream));
    }

    if (impl_->packet->stream_index == impl_->video_stream) {
      if (avcodec_send_packet(impl_->video_codec, impl_->packet) < 0) {
        av_packet_unref(impl_->packet);
        return std::unexpected(Error(ErrorKind::decode_failure));
      }
    } else if (impl_->packet->stream_index == impl_->audio_stream) {
      // Audio packets are handled by pump_audio(); skip here.
    }
    av_packet_unref(impl_->packet);
  }
}

VoidResult VideoSource::pump_audio(PcmRing& ring) noexcept {
  if (!impl_ || !impl_->opened || impl_->audio_stream < 0 || !impl_->audio_codec ||
      !impl_->swr || impl_->audio_eof) {
    return {};
  }

  auto resample_and_write = [&](AVFrame* decoded) -> VoidResult {
    const int out_samples = swr_get_out_samples(impl_->swr, decoded->nb_samples);
    if (out_samples <= 0) {
      return {};
    }
    const std::size_t need_bytes = static_cast<std::size_t>(out_samples) *
                                   impl_->audio_format.channels *
                                   sizeof(std::int16_t);
    if (impl_->swr_buffer.size() < need_bytes) {
      impl_->swr_buffer.resize(need_bytes);
    }

    std::uint8_t* out_planes[1] = {impl_->swr_buffer.data()};
    const int got = swr_convert(impl_->swr, out_planes, out_samples,
                                const_cast<const std::uint8_t**>(decoded->data),
                                decoded->nb_samples);
    if (got < 0) {
      return std::unexpected(Error(ErrorKind::decode_failure));
    }
    if (got > 0) {
      ring.write(std::span(
          reinterpret_cast<const std::int16_t*>(impl_->swr_buffer.data()),
          static_cast<std::size_t>(got) * impl_->audio_format.channels));
    }
    return {};
  };

  // Drain any already-decoded audio frames first.
  for (;;) {
    const int rec = avcodec_receive_frame(impl_->audio_codec, impl_->audio_frame);
    if (rec == 0) {
      auto conv = resample_and_write(impl_->audio_frame);
      av_frame_unref(impl_->audio_frame);
      if (!conv) {
        return std::unexpected(conv.error());
      }
      continue;
    }
    if (rec == AVERROR_EOF) {
      impl_->audio_eof = true;
      return {};
    }
    if (rec != AVERROR(EAGAIN)) {
      return std::unexpected(Error(ErrorKind::decode_failure));
    }
    break;
  }

  // Read packets until an audio packet is found or EOF.
  for (;;) {
    const int rr = av_read_frame(impl_->fmt, impl_->packet);
    if (rr < 0) {
      avcodec_send_packet(impl_->audio_codec, nullptr);
      for (;;) {
        const int rec = avcodec_receive_frame(impl_->audio_codec, impl_->audio_frame);
        if (rec == 0) {
          auto conv = resample_and_write(impl_->audio_frame);
          av_frame_unref(impl_->audio_frame);
          if (!conv) {
            return std::unexpected(conv.error());
          }
          continue;
        }
        if (rec == AVERROR_EOF) {
          impl_->audio_eof = true;
          return {};
        }
        if (rec != AVERROR(EAGAIN)) {
          return std::unexpected(Error(ErrorKind::decode_failure));
        }
        break;
      }
      impl_->audio_eof = true;
      return {};
    }

    if (impl_->packet->stream_index != impl_->audio_stream) {
      av_packet_unref(impl_->packet);
      continue;
    }

    if (avcodec_send_packet(impl_->audio_codec, impl_->packet) < 0) {
      av_packet_unref(impl_->packet);
      return std::unexpected(Error(ErrorKind::decode_failure));
    }
    av_packet_unref(impl_->packet);

    // Try to receive at least one frame from this packet.
    for (;;) {
      const int rec = avcodec_receive_frame(impl_->audio_codec, impl_->audio_frame);
      if (rec == 0) {
        auto conv = resample_and_write(impl_->audio_frame);
        av_frame_unref(impl_->audio_frame);
        if (!conv) {
          return std::unexpected(conv.error());
        }
        continue;
      }
      if (rec == AVERROR_EOF) {
        impl_->audio_eof = true;
        return {};
      }
      if (rec == AVERROR(EAGAIN)) {
        break;
      }
      return std::unexpected(Error(ErrorKind::decode_failure));
    }

    // Stop after one audio packet so the caller can also pump video.
    return {};
  }
}

VoidResult VideoSource::seek(double seconds) {
  if (!impl_ || !impl_->opened) {
    return std::unexpected(Error(ErrorKind::invalid_argument));
  }

  const auto target = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
  const int rc = av_seek_frame(impl_->fmt, -1, target, AVSEEK_FLAG_BACKWARD);
  if (rc < 0) {
    return std::unexpected(Error(ErrorKind::ffmpeg_error));
  }

  avcodec_flush_buffers(impl_->video_codec);
  if (impl_->audio_codec) {
    avcodec_flush_buffers(impl_->audio_codec);
  }
  impl_->eof = false;
  impl_->audio_eof = false;
  return {};
}

double VideoSource::duration() const noexcept {
  if (!impl_ || !impl_->fmt) {
    return 0.0;
  }
  if (impl_->fmt->duration == AV_NOPTS_VALUE) {
    return 0.0;
  }
  return static_cast<double>(impl_->fmt->duration) / AV_TIME_BASE;
}

} // namespace zola
