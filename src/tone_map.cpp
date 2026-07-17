#include "zola/tone_map.hpp"

#include <algorithm>
#include <cmath>

namespace zola {
namespace {

[[nodiscard]] double clamp01(double t) noexcept {
  if (t < 0.0) {
    return 0.0;
  }
  if (t > 1.0) {
    return 1.0;
  }
  return t;
}

[[nodiscard]] bool near_one(double x) noexcept {
  return std::abs(x - 1.0) < 1e-12;
}

[[nodiscard]] bool near_zero(double x) noexcept {
  return std::abs(x) < 1e-12;
}

} // namespace

ToneMap::ToneMap(ToneMapParams params) : params_(params) {
  if (params_.contrast <= 0.0) {
    params_.contrast = 1.0;
  }
  if (params_.gamma <= 0.0) {
    params_.gamma = 1.0;
  }
  params_.low_percentile = std::clamp(params_.low_percentile, 0.0, 1.0);
  params_.high_percentile = std::clamp(params_.high_percentile, 0.0, 1.0);
  if (params_.high_percentile < params_.low_percentile) {
    std::swap(params_.low_percentile, params_.high_percentile);
  }
}

bool ToneMap::is_identity() const noexcept {
  return near_zero(params_.brightness) && near_one(params_.contrast) &&
         near_one(params_.gamma) && !params_.auto_levels;
}

std::uint8_t ToneMap::map_sample(std::uint8_t y, std::uint8_t auto_lo,
                                 std::uint8_t auto_hi) const noexcept {
  double t = static_cast<double>(y) / 255.0;

  // Optional auto-levels stretch (already resolved to lo/hi for the frame).
  if (params_.auto_levels) {
    if (auto_hi > auto_lo) {
      t = (static_cast<double>(y) - static_cast<double>(auto_lo)) /
          (static_cast<double>(auto_hi) - static_cast<double>(auto_lo));
      t = clamp01(t);
    } else {
      // Degenerate: flat frame — keep original normalized value.
      t = static_cast<double>(y) / 255.0;
    }
  }

  // Gamma on normalized [0,1].
  if (!near_one(params_.gamma)) {
    t = std::pow(clamp01(t), params_.gamma);
  }

  // Contrast around mid-gray, then brightness bias.
  t = (t - 0.5) * params_.contrast + 0.5 + params_.brightness;
  t = clamp01(t);

  return static_cast<std::uint8_t>(t * 255.0 + 0.5);
}

void ToneMap::compute_auto_levels(const Frame& frame, std::uint8_t& lo,
                                  std::uint8_t& hi) const {
  // 256-bin histogram; stack-only, no heap.
  std::uint32_t hist[256]{};
  const auto samples = frame.samples();
  for (const std::uint8_t s : samples) {
    ++hist[s];
  }

  const std::size_t n = samples.size();
  if (n == 0) {
    lo = 0;
    hi = 255;
    return;
  }

  const auto rank_at = [&](double pct) -> std::uint8_t {
    // Value at 0-based sample index floor(pct * (n-1)).
    const std::size_t target =
        static_cast<std::size_t>(pct * static_cast<double>(n - 1));
    std::size_t cumulative = 0;
    for (int i = 0; i < 256; ++i) {
      cumulative += hist[i];
      if (cumulative > target) {
        return static_cast<std::uint8_t>(i);
      }
    }
    return 255;
  };

  lo = rank_at(params_.low_percentile);
  hi = rank_at(params_.high_percentile);
  if (hi < lo) {
    std::swap(hi, lo);
  }
}

void ToneMap::apply(Frame& frame) const {
  if (frame.empty() || is_identity()) {
    return;
  }

  std::uint8_t lo = 0;
  std::uint8_t hi = 255;
  if (params_.auto_levels) {
    compute_auto_levels(frame, lo, hi);
  }

  auto samples = frame.samples();
  for (std::uint8_t& s : samples) {
    s = map_sample(s, lo, hi);
  }
}

void ToneMap::apply(const Frame& in, Frame& out) const {
  out.ensure_size(in.width(), in.height());
  if (in.empty()) {
    return;
  }

  if (is_identity()) {
    const auto src = in.samples();
    auto dst = out.samples();
    std::copy(src.begin(), src.end(), dst.begin());
    return;
  }

  std::uint8_t lo = 0;
  std::uint8_t hi = 255;
  if (params_.auto_levels) {
    compute_auto_levels(in, lo, hi);
  }

  const auto src = in.samples();
  auto dst = out.samples();
  for (std::size_t i = 0; i < src.size(); ++i) {
    dst[i] = map_sample(src[i], lo, hi);
  }
}

} // namespace zola
