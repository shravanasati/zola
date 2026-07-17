#pragma once

#include "zola/frame.hpp"
#include "zola/glyph_ramp.hpp"

#include <cstddef>

namespace zola {

/// Maps a Frame to a Cell grid using a Glyph ramp.
/// Deep module: owns downsample + luminance→glyph; pure and allocation-light
/// when the output grid is pre-sized via ensure_size.
class AsciiMapper {
public:
  explicit AsciiMapper(GlyphRamp ramp = GlyphRamp{}) : ramp_(ramp) {}

  void set_ramp(GlyphRamp ramp) { ramp_ = ramp; }
  [[nodiscard]] const GlyphRamp& ramp() const noexcept { return ramp_; }

  /// Vertical sample bias: terminal cells are typically ~2× taller than wide.
  /// Source rows per output row when preserving aspect (default 2.0).
  void set_char_aspect(double aspect) { char_aspect_ = aspect > 0.0 ? aspect : 2.0; }
  [[nodiscard]] double char_aspect() const noexcept { return char_aspect_; }

  /// Map entire frame into out (resized to cols × rows).
  /// Glyph from box-averaged gray; when map_color and frame.has_color(), also
  /// box-average RGB into each Cell over the same source block.
  void map(const Frame& frame, std::size_t cols, std::size_t rows,
           CellGrid& out, bool map_color = false) const;

  /// Compute cols/rows that fit max_cols × max_rows while preserving frame
  /// aspect (accounting for character cell aspect).
  void fit_size(std::size_t frame_w, std::size_t frame_h, std::size_t max_cols,
                std::size_t max_rows, std::size_t& out_cols,
                std::size_t& out_rows) const;

private:
  GlyphRamp ramp_;
  double char_aspect_ = 2.0;
};

} // namespace zola
