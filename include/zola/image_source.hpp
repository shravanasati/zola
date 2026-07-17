#pragma once

#include "zola/source.hpp"

#include <filesystem>
#include <string>

namespace zola {

/// Still-image Source backed by stb_image. Yields one Frame (gray + RGB24).
class ImageSource final : public Source {
public:
  explicit ImageSource(std::filesystem::path path);

  VoidResult open() override;
  Result<bool> next_frame(Frame& out) override;

  [[nodiscard]] std::size_t width() const noexcept override { return width_; }
  [[nodiscard]] std::size_t height() const noexcept override { return height_; }

private:
  std::filesystem::path path_;
  Frame frame_;
  std::size_t width_ = 0;
  std::size_t height_ = 0;
  bool consumed_ = false;
  bool opened_ = false;
};

} // namespace zola
