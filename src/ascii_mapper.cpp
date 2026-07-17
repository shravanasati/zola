#include "zola/ascii_mapper.hpp"

#include <algorithm>
#include <cmath>

namespace zola {

void AsciiMapper::fit_size(std::size_t frame_w, std::size_t frame_h,
                           std::size_t max_cols, std::size_t max_rows,
                           std::size_t& out_cols,
                           std::size_t& out_rows) const {
  if (frame_w == 0 || frame_h == 0 || max_cols == 0 || max_rows == 0) {
    out_cols = 0;
    out_rows = 0;
    return;
  }

  // Character cells are taller than wide; treat source height as compressed.
  const double aspect =
      (static_cast<double>(frame_w) / static_cast<double>(frame_h)) *
      char_aspect_;

  double cols = static_cast<double>(max_cols);
  double rows = cols / aspect;
  if (rows > static_cast<double>(max_rows)) {
    rows = static_cast<double>(max_rows);
    cols = rows * aspect;
  }

  out_cols = std::max<std::size_t>(1, static_cast<std::size_t>(cols));
  out_rows = std::max<std::size_t>(1, static_cast<std::size_t>(rows));
  out_cols = std::min(out_cols, max_cols);
  out_rows = std::min(out_rows, max_rows);
}

void AsciiMapper::map(const Frame& frame, std::size_t cols, std::size_t rows,
                      CellGrid& out, bool map_color) const {
  if (cols == 0 || rows == 0 || frame.empty()) {
    out.resize(0, 0);
    return;
  }

  out.ensure_size(cols, rows);

  const std::size_t src_w = frame.width();
  const std::size_t src_h = frame.height();
  const std::uint8_t* src = frame.data();
  const bool do_color = map_color && frame.has_color();
  const std::uint8_t* rgb = do_color ? frame.rgb().data() : nullptr;

  for (std::size_t y = 0; y < rows; ++y) {
    const std::size_t y0 = (y * src_h) / rows;
    const std::size_t y1 = std::max(y0 + 1, ((y + 1) * src_h) / rows);

    for (std::size_t x = 0; x < cols; ++x) {
      const std::size_t x0 = (x * src_w) / cols;
      const std::size_t x1 = std::max(x0 + 1, ((x + 1) * src_w) / cols);

      // Box average of the source block (gray always; RGB when requested).
      std::uint64_t sum = 0;
      std::uint64_t sum_r = 0;
      std::uint64_t sum_g = 0;
      std::uint64_t sum_b = 0;
      std::uint64_t count = 0;
      for (std::size_t sy = y0; sy < y1; ++sy) {
        const std::uint8_t* row = src + sy * src_w;
        const std::uint8_t* rgb_row =
            do_color ? rgb + sy * src_w * 3 : nullptr;
        for (std::size_t sx = x0; sx < x1; ++sx) {
          sum += row[sx];
          if (do_color) {
            const std::uint8_t* p = rgb_row + sx * 3;
            sum_r += p[0];
            sum_g += p[1];
            sum_b += p[2];
          }
          ++count;
        }
      }
      Cell& cell = out.at(x, y);
      const std::uint8_t lum =
          count == 0 ? 0
                     : static_cast<std::uint8_t>(sum / count);
      cell.glyph = ramp_.from_luminance(lum);
      if (do_color && count > 0) {
        cell.r = static_cast<std::uint8_t>(sum_r / count);
        cell.g = static_cast<std::uint8_t>(sum_g / count);
        cell.b = static_cast<std::uint8_t>(sum_b / count);
      } else {
        // Mono-safe default (or gray-only frame).
        cell.r = 255;
        cell.g = 255;
        cell.b = 255;
      }
    }
  }
}

} // namespace zola
