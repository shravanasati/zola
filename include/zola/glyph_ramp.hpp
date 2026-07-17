#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zola {

/// Ordered ASCII density from darkest to lightest (for dark terminal backgrounds).
class GlyphRamp {
public:
  static constexpr std::string_view kDefault = " .:-=+*#%@";

  explicit GlyphRamp(std::string_view glyphs = kDefault) : glyphs_(glyphs) {}

  [[nodiscard]] std::string_view glyphs() const noexcept { return glyphs_; }
  [[nodiscard]] std::size_t size() const noexcept { return glyphs_.size(); }
  [[nodiscard]] bool empty() const noexcept { return glyphs_.empty(); }

  /// Map luminance 0–255 to a glyph. 0 → darkest, 255 → lightest.
  [[nodiscard]] char from_luminance(std::uint8_t y) const noexcept {
    if (glyphs_.empty()) {
      return ' ';
    }
    const std::size_t n = glyphs_.size();
    // Map 0..255 onto 0..n-1 inclusive without overflow.
    const std::size_t idx =
        (static_cast<std::size_t>(y) * n) / 256;
    return glyphs_[idx < n ? idx : n - 1];
  }

private:
  std::string_view glyphs_;
};

} // namespace zola
