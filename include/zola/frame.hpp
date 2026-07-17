#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace zola {

/// Intermediate representation: grayscale plane always; optional interleaved
/// RGB24 plane (same WxH) when Sources fill color.
class Frame {
public:
  Frame() = default;

  Frame(std::size_t width, std::size_t height)
      : width_(width), height_(height), data_(width * height, 0) {}

  [[nodiscard]] std::size_t width() const noexcept { return width_; }
  [[nodiscard]] std::size_t height() const noexcept { return height_; }
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  /// True when rgb_ holds a full interleaved RGB24 plane for current WxH.
  [[nodiscard]] bool has_color() const noexcept {
    return width_ > 0 && height_ > 0 &&
           rgb_.size() == width_ * height_ * 3;
  }

  /// Allocate/clear RGB plane to width_*height_*3 (idempotent if already sized).
  void ensure_color() {
    const std::size_t need = width_ * height_ * 3;
    if (rgb_.size() != need) {
      rgb_.assign(need, 0);
    }
  }

  /// Drop RGB plane (gray unchanged). Sources re-ensure_color when needed.
  void clear_color() noexcept { rgb_.clear(); }

  void resize(std::size_t width, std::size_t height) {
    width_ = width;
    height_ = height;
    data_.assign(width * height, 0);
    // Drop RGB on resize; Sources refill via ensure_color().
    rgb_.clear();
  }

  /// Ensure capacity matches width*height without clearing if size already matches.
  void ensure_size(std::size_t width, std::size_t height) {
    if (width_ == width && height_ == height && data_.size() == width * height) {
      return;
    }
    resize(width, height);
  }

  [[nodiscard]] std::uint8_t& at(std::size_t x, std::size_t y) {
    return data_[y * width_ + x];
  }

  [[nodiscard]] std::uint8_t at(std::size_t x, std::size_t y) const {
    return data_[y * width_ + x];
  }

  [[nodiscard]] std::span<std::uint8_t> samples() noexcept { return data_; }
  [[nodiscard]] std::span<const std::uint8_t> samples() const noexcept {
    return data_;
  }

  [[nodiscard]] std::uint8_t* data() noexcept { return data_.data(); }
  [[nodiscard]] const std::uint8_t* data() const noexcept { return data_.data(); }

  /// Interleaved RGB24, size = W*H*3 when has_color(); empty otherwise.
  [[nodiscard]] std::span<std::uint8_t> rgb() noexcept { return rgb_; }
  [[nodiscard]] std::span<const std::uint8_t> rgb() const noexcept {
    return rgb_;
  }

private:
  std::size_t width_ = 0;
  std::size_t height_ = 0;
  std::vector<std::uint8_t> data_;
  std::vector<std::uint8_t> rgb_;
};

/// One terminal character position.
struct Cell {
  char glyph = ' ';
  /// Color sample (ignored in mono Presenter mode). Default white is mono-safe.
  std::uint8_t r = 255, g = 255, b = 255;
};

/// Rectangular array of Cells ready for presentation.
class CellGrid {
public:
  CellGrid() = default;

  CellGrid(std::size_t cols, std::size_t rows)
      : cols_(cols), rows_(rows), cells_(cols * rows) {}

  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }

  void resize(std::size_t cols, std::size_t rows) {
    cols_ = cols;
    rows_ = rows;
    cells_.assign(cols * rows, Cell{});
  }

  void ensure_size(std::size_t cols, std::size_t rows) {
    if (cols_ == cols && rows_ == rows && cells_.size() == cols * rows) {
      return;
    }
    resize(cols, rows);
  }

  [[nodiscard]] Cell& at(std::size_t x, std::size_t y) {
    return cells_[y * cols_ + x];
  }

  [[nodiscard]] const Cell& at(std::size_t x, std::size_t y) const {
    return cells_[y * cols_ + x];
  }

  [[nodiscard]] std::span<Cell> cells() noexcept { return cells_; }
  [[nodiscard]] std::span<const Cell> cells() const noexcept { return cells_; }

private:
  std::size_t cols_ = 0;
  std::size_t rows_ = 0;
  std::vector<Cell> cells_;
};

} // namespace zola
