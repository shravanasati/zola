#pragma once

#include "zola/error.hpp"
#include "zola/pcm_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace zola {

struct AudioFormat {
  int sample_rate = 0;
  int channels = 2; // v1: always stereo after swr
};

/// Device backend that consumes interleaved S16 PCM. Abstracts miniaudio so the
/// Engine only sees open/start/stop and a shared PcmRing.
class AudioOutput {
public:
  AudioOutput();
  ~AudioOutput();

  AudioOutput(const AudioOutput&) = delete;
  AudioOutput& operator=(const AudioOutput&) = delete;
  AudioOutput(AudioOutput&&) = delete;
  AudioOutput& operator=(AudioOutput&&) = delete;

  /// Open the default audio device at the requested format. On failure, returns
  /// an Error but leaves the object in a stopped, safe state.
  VoidResult open(const AudioFormat& format);

  /// Start the device callback (pulling from the ring supplied at open).
  void start();

  /// Suspend the device callback without tearing down the device/context.
  void pause() noexcept;

  /// Resume a paused device callback.
  void resume() noexcept;

  /// Stop and close the device. Safe to call multiple times; noexcept because
  /// this is used in teardown / SIGINT paths.
  void stop() noexcept;

  [[nodiscard]] bool is_open() const noexcept;

  /// Number of samples consumed by the device since start. Used as the audio
  /// clock for A/V sync when playing.
  [[nodiscard]] std::size_t samples_played() const noexcept;

  /// Set the master volume factor (0 = silence, 1 = full). Safe to call
  /// before or after start; has no effect if the device is not open.
  void set_volume(float volume) noexcept;

  [[nodiscard]] PcmRing& ring() noexcept { return ring_; }
  [[nodiscard]] const PcmRing& ring() const noexcept { return ring_; }

  void add_samples_played(std::size_t n) noexcept;

  /// Reset the samples-played counter to an absolute position (used after seek).
  void set_samples_played(std::size_t n) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  AudioFormat format_;
  PcmRing ring_;
  std::atomic<std::size_t> samples_played_{0};
};

} // namespace zola
