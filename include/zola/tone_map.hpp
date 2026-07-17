#pragma once

#include "zola/frame.hpp"

#include <cstddef>
#include <cstdint>

namespace zola {

/// Parameters for remapping sample luminances before glyph mapping.
/// Sources stay linear-ish 0–255; ToneMap is the only place brightness lives.
struct ToneMapParams {
  /// Additive bias after contrast (`t = … + brightness`). Typical range ~[-1, 1].
  double brightness = 0.0;
  /// Multiplicative gain around mid-gray. 1 = identity.
  double contrast = 1.0;
  /// Power curve on normalized luminance before contrast. 1 = identity.
  double gamma = 1.0;
  /// Stretch low/high percentiles of the frame to full 0–255 range.
  bool auto_levels = false;
  /// Inclusive percentile fraction for auto-levels low/high (0–1).
  double low_percentile = 0.01;
  double high_percentile = 0.99;
};

/// Pure module: remap Frame luminances (gain, bias, gamma, auto-levels).
/// No I/O, no ANSI. Hot path is allocation-free after construction.
class ToneMap {
public:
  explicit ToneMap(ToneMapParams params = {});

  [[nodiscard]] const ToneMapParams& params() const noexcept { return params_; }

  /// True when apply would leave every sample unchanged (skip on hot path).
  [[nodiscard]] bool is_identity() const noexcept;

  /// In-place remap of frame samples.
  void apply(Frame& frame) const;

  /// Copy-and-remap into out (resized to match in).
  void apply(const Frame& in, Frame& out) const;

  /// Map a single normalized-or-byte sample after optional auto-levels stretch.
  /// Used by tests; auto_lo/auto_hi are stretch endpoints in 0–255 space
  /// (pass 0 and 255 when auto-levels is off).
  [[nodiscard]] std::uint8_t map_sample(std::uint8_t y, std::uint8_t auto_lo,
                                        std::uint8_t auto_hi) const noexcept;

private:
  ToneMapParams params_;

  void compute_auto_levels(const Frame& frame, std::uint8_t& lo,
                           std::uint8_t& hi) const;
};

} // namespace zola
