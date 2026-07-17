#pragma once

#include "zola/source.hpp"

#include <filesystem>
#include <memory>

namespace zola {

/// Silent video Source using FFmpeg (libav*). Yields grayscale Frames.
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

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace zola
