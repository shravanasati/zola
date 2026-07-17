#include "zola/video_source.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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
  AVCodecContext* codec = nullptr;
  const AVCodec* decoder = nullptr;
  SwsContext* sws = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* rgb = nullptr; // swscale dest: RGB24
  AVPacket* packet = nullptr;
  int video_stream = -1;
  std::size_t width = 0;
  std::size_t height = 0;
  double fps = 0.0;
  bool opened = false;
  bool eof = false;

  void close() noexcept {
    if (sws) {
      sws_freeContext(sws);
      sws = nullptr;
    }
    if (rgb) {
      av_frame_free(&rgb);
    }
    if (frame) {
      av_frame_free(&frame);
    }
    if (packet) {
      av_packet_free(&packet);
    }
    if (codec) {
      avcodec_free_context(&codec);
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

VoidResult VideoSource::open() {
  if (!impl_) {
    return std::unexpected(Error::invalid_argument);
  }
  if (impl_->opened) {
    return {};
  }

  const std::string path = impl_->path.string();
  if (avformat_open_input(&impl_->fmt, path.c_str(), nullptr, nullptr) < 0) {
    return std::unexpected(Error::io_failure);
  }
  if (avformat_find_stream_info(impl_->fmt, nullptr) < 0) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }

  impl_->video_stream = av_find_best_stream(impl_->fmt, AVMEDIA_TYPE_VIDEO, -1,
                                            -1, &impl_->decoder, 0);
  if (impl_->video_stream < 0 || !impl_->decoder) {
    impl_->close();
    return std::unexpected(Error::unsupported);
  }

  AVStream* stream = impl_->fmt->streams[impl_->video_stream];
  impl_->codec = avcodec_alloc_context3(impl_->decoder);
  if (!impl_->codec) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }
  if (avcodec_parameters_to_context(impl_->codec, stream->codecpar) < 0) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }
  if (avcodec_open2(impl_->codec, impl_->decoder, nullptr) < 0) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }

  impl_->width = static_cast<std::size_t>(impl_->codec->width);
  impl_->height = static_cast<std::size_t>(impl_->codec->height);
  if (impl_->width == 0 || impl_->height == 0) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
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
    return std::unexpected(Error::decode_failure);
  }

  // Destination RGB24 buffer owned by rgb frame (preallocated once).
  impl_->rgb->format = AV_PIX_FMT_RGB24;
  impl_->rgb->width = impl_->codec->width;
  impl_->rgb->height = impl_->codec->height;
  if (av_frame_get_buffer(impl_->rgb, 32) < 0) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }

  impl_->sws =
      sws_getContext(impl_->codec->width, impl_->codec->height,
                     impl_->codec->pix_fmt, impl_->codec->width,
                     impl_->codec->height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                     nullptr, nullptr, nullptr);
  if (!impl_->sws) {
    impl_->close();
    return std::unexpected(Error::decode_failure);
  }

  impl_->opened = true;
  impl_->eof = false;
  return {};
}

Result<bool> VideoSource::next_frame(Frame& out) {
  if (!impl_ || !impl_->opened) {
    return std::unexpected(Error::invalid_argument);
  }
  if (impl_->eof) {
    return std::unexpected(Error::end_of_stream);
  }

  auto convert_to_out = [&](AVFrame* decoded) -> VoidResult {
    if (av_frame_make_writable(impl_->rgb) < 0) {
      return std::unexpected(Error::decode_failure);
    }
    sws_scale(impl_->sws, decoded->data, decoded->linesize, 0,
              impl_->codec->height, impl_->rgb->data, impl_->rgb->linesize);

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
    return {};
  };

  // Drain / read until we get one video frame.
  for (;;) {
    // Try receive first (handles buffered frames).
    const int rec = avcodec_receive_frame(impl_->codec, impl_->frame);
    if (rec == 0) {
      auto conv = convert_to_out(impl_->frame);
      av_frame_unref(impl_->frame);
      if (!conv) {
        return std::unexpected(conv.error());
      }
      return true;
    }
    if (rec != AVERROR(EAGAIN)) {
      if (rec == AVERROR_EOF) {
        impl_->eof = true;
        return std::unexpected(Error::end_of_stream);
      }
      return std::unexpected(Error::decode_failure);
    }

    // Need more input.
    const int rr = av_read_frame(impl_->fmt, impl_->packet);
    if (rr < 0) {
      // Flush decoder.
      avcodec_send_packet(impl_->codec, nullptr);
      const int rec2 = avcodec_receive_frame(impl_->codec, impl_->frame);
      if (rec2 == 0) {
        auto conv = convert_to_out(impl_->frame);
        av_frame_unref(impl_->frame);
        if (!conv) {
          return std::unexpected(conv.error());
        }
        return true;
      }
      impl_->eof = true;
      return std::unexpected(Error::end_of_stream);
    }

    if (impl_->packet->stream_index != impl_->video_stream) {
      av_packet_unref(impl_->packet);
      continue;
    }

    if (avcodec_send_packet(impl_->codec, impl_->packet) < 0) {
      av_packet_unref(impl_->packet);
      return std::unexpected(Error::decode_failure);
    }
    av_packet_unref(impl_->packet);
  }
}

} // namespace zola
