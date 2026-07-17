#include "zola/image_source.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include <algorithm>
#include <cmath>

namespace zola {
namespace {

std::uint8_t luminance_u8(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  // Rec. 709
  const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  return static_cast<std::uint8_t>(std::lround(std::clamp(y, 0.0, 255.0)));
}

} // namespace

ImageSource::ImageSource(std::filesystem::path path) : path_(std::move(path)) {}

VoidResult ImageSource::open() {
  if (opened_) {
    return {};
  }

  int w = 0;
  int h = 0;
  int channels = 0;
  const std::string path_str = path_.string();
  unsigned char* pixels =
      stbi_load(path_str.c_str(), &w, &h, &channels, 3);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels) {
      stbi_image_free(pixels);
    }
    return std::unexpected(Error::decode_failure);
  }

  width_ = static_cast<std::size_t>(w);
  height_ = static_cast<std::size_t>(h);
  frame_.ensure_size(width_, height_);
  frame_.ensure_color();

  const unsigned char* p = pixels;
  auto* gray = frame_.data();
  auto* rgb = frame_.rgb().data();
  for (std::size_t i = 0; i < width_ * height_; ++i) {
    rgb[0] = p[0];
    rgb[1] = p[1];
    rgb[2] = p[2];
    gray[i] = luminance_u8(p[0], p[1], p[2]);
    p += 3;
    rgb += 3;
  }

  stbi_image_free(pixels);
  consumed_ = false;
  opened_ = true;
  return {};
}

Result<bool> ImageSource::next_frame(Frame& out) {
  if (!opened_) {
    return std::unexpected(Error::invalid_argument);
  }
  if (consumed_) {
    return std::unexpected(Error::end_of_stream);
  }

  out.ensure_size(width_, height_);
  std::copy(frame_.samples().begin(), frame_.samples().end(),
            out.samples().begin());
  if (frame_.has_color()) {
    out.ensure_color();
    std::copy(frame_.rgb().begin(), frame_.rgb().end(), out.rgb().begin());
  }
  consumed_ = true;
  return true;
}

} // namespace zola
