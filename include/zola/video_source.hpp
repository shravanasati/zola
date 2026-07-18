#pragma once

#include "zola/audio_output.hpp"
#include "zola/pcm_ring.hpp"
#include "zola/source.hpp"

#include <filesystem>
#include <memory>

namespace zola {

/// Video Source using FFmpeg (libav*). Yields grayscale Frames and optionally
/// decodes container audio into a PcmRing.
class VideoSource final : public Source {
public:
  explicit VideoSource(std::filesystem::path path);
  ~VideoSource() override;

  VideoSource(const VideoSource&) = delete;
  VideoSource& operator=(const VideoSource&) = delete;
  VideoSource(VideoSource&&) noexcept;
  VideoSource& operator=(VideoSource&&) noexcept;

  VoidResult open() override;
  Result<bool> next_frame(Frame& out) override;

  [[nodiscard]] std::size_t width() const noexcept override;
  [[nodiscard]] std::size_t height() const noexcept override;
  [[nodiscard]] double fps() const noexcept override;

  [[nodiscard]] bool has_audio() const noexcept;
  [[nodiscard]] AudioFormat audio_format() const noexcept;

  /// Decode and enqueue any available audio packets into ring. Called by Engine
  /// to keep the audio device fed. Safe to call when has_audio() is false.
  VoidResult pump_audio(PcmRing& ring) noexcept;

  /// Seek to the given time (seconds from start). Flushes both decoders.
  VoidResult seek(double seconds);

  /// Container duration in seconds, or 0 if unknown.
  [[nodiscard]] double duration() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace zola
