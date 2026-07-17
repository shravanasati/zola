#pragma once

#include "zola/ascii_mapper.hpp"
#include "zola/error.hpp"
#include "zola/glyph_ramp.hpp"
#include "zola/presenter.hpp"
#include "zola/tone_map.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace zola {

struct EngineOptions {
  /// 0 = use full terminal dimension.
  std::size_t cols = 0;
  std::size_t rows = 0;
  /// 0 = use container FPS (video) or default 24.
  double fps_override = 0.0;
  GlyphRamp ramp{};
  bool alt_screen = true;
  /// Reverse glyph ramp for light terminal backgrounds (separate from ToneMap).
  bool invert = false;
  ToneMapParams tone{};
  ColorMode color = ColorMode::mono;
  /// When true and color is truecolor: SGR background + foreground from Cell RGB.
  bool color_bg = false;
};

/// Orchestrates Source → ToneMap → Mapper → Presenter.
class Engine {
public:
  explicit Engine(EngineOptions opts = {});

  VoidResult show_image(const std::filesystem::path& path);
  VoidResult play_video(const std::filesystem::path& path);

private:
  EngineOptions opts_;
  /// Owns reversed glyphs when `invert` is set (GlyphRamp holds string_view).
  std::string invert_ramp_storage_;
  ToneMap tone_;
  AsciiMapper mapper_;
  Presenter presenter_;
  Frame frame_;
  CellGrid grid_;

  void resolve_output_size(std::size_t src_w, std::size_t src_h,
                           std::size_t& cols, std::size_t& rows) const;

  [[nodiscard]] bool map_color() const noexcept {
    return opts_.color != ColorMode::mono;
  }
};

} // namespace zola
