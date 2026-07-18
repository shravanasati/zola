#include "zola/audio_output.hpp"

#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_SOUND
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

namespace zola {

struct AudioOutput::Impl {
  ma_context context{};
  ma_device device{};
  bool context_initialized = false;
  bool device_initialized = false;
};

namespace {

void data_callback(ma_device* device, void* output, const void* /*input*/,
                   ma_uint32 frame_count) {
  auto* out = static_cast<AudioOutput*>(device->pUserData);
  if (!out || !out->is_open()) {
    std::memset(output, 0,
                frame_count * device->playback.channels * sizeof(std::int16_t));
    return;
  }

  const std::size_t channels = device->playback.channels;
  const std::size_t samples = static_cast<std::size_t>(frame_count) * channels;
  const std::size_t read =
      out->ring().read({static_cast<std::int16_t*>(output), samples});
  if (read < samples) {
    std::memset(static_cast<std::int16_t*>(output) + read, 0,
                (samples - read) * sizeof(std::int16_t));
  }
  out->add_samples_played(samples);
}

} // namespace

AudioOutput::AudioOutput()
    : impl_(std::make_unique<Impl>()), ring_(48000 * 2) {}

AudioOutput::~AudioOutput() { stop(); }

VoidResult AudioOutput::open(const AudioFormat& format) {
  stop();

  if (format.sample_rate <= 0 || format.channels <= 0) {
    return std::unexpected(Error(ErrorKind::invalid_argument));
  }

  format_ = format;
  samples_played_.store(0, std::memory_order_relaxed);
  ring_.clear();
  if (ring_.capacity() !=
      static_cast<std::size_t>(format_.sample_rate) * format_.channels) {
    ring_ = PcmRing(static_cast<std::size_t>(format_.sample_rate) *
                    static_cast<std::size_t>(format_.channels));
  }

  ma_result res = ma_context_init(nullptr, 0, nullptr, &impl_->context);
  if (res != MA_SUCCESS) {
    return std::unexpected(Error(ErrorKind::io_failure));
  }
  impl_->context_initialized = true;

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_s16;
  config.playback.channels = static_cast<ma_uint32>(format_.channels);
  config.sampleRate = static_cast<ma_uint32>(format_.sample_rate);
  config.dataCallback = data_callback;
  config.pUserData = this;

  res = ma_device_init(&impl_->context, &config, &impl_->device);
  if (res != MA_SUCCESS) {
    stop();
    return std::unexpected(Error(ErrorKind::io_failure));
  }
  impl_->device_initialized = true;

  return {};
}

void AudioOutput::start() {
  if (!impl_->device_initialized) {
    return;
  }
  ma_device_start(&impl_->device);
}

void AudioOutput::pause() noexcept {
  if (!impl_->device_initialized) {
    return;
  }
  ma_device_stop(&impl_->device);
}

void AudioOutput::resume() noexcept {
  if (!impl_->device_initialized) {
    return;
  }
  ma_device_start(&impl_->device);
}

void AudioOutput::stop() noexcept {
  if (impl_->device_initialized) {
    ma_device_stop(&impl_->device);
    ma_device_uninit(&impl_->device);
    impl_->device_initialized = false;
  }
  if (impl_->context_initialized) {
    ma_context_uninit(&impl_->context);
    impl_->context_initialized = false;
  }
}

bool AudioOutput::is_open() const noexcept { return impl_->device_initialized; }

std::size_t AudioOutput::samples_played() const noexcept {
  return samples_played_.load(std::memory_order_relaxed);
}

void AudioOutput::add_samples_played(std::size_t n) noexcept {
  samples_played_.fetch_add(n, std::memory_order_relaxed);
}

void AudioOutput::set_samples_played(std::size_t n) noexcept {
  samples_played_.store(n, std::memory_order_relaxed);
}

} // namespace zola
