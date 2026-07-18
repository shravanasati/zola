#pragma once

#include "zola/error.hpp"
#include "zola/frame.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <termios.h>

namespace zola {

/// Terminal size in character cells.
struct TerminalSize {
  std::size_t cols = 80;
  std::size_t rows = 24;
};

/// How the Presenter emits Cell color samples as SGR (if at all).
enum class ColorMode {
  mono,      ///< Glyphs only; no color SGR.
  truecolor, ///< 24-bit foreground (and optional background) SGR.
  // ansi256, ansi16 — later
};

/// Pure: append Cell grid text (+ optional truecolor SGR with RLE) into out.
/// Does not emit cursor-home; used by Presenter and unit tests.
void append_grid(const CellGrid& grid, ColorMode mode, bool color_bg,
                 std::string& out);

/// Writes a Cell grid to the terminal (cursor home + overwrite).
class Presenter {
public:
  Presenter() = default;
  ~Presenter();

  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;

  void set_color_mode(ColorMode mode) noexcept { color_mode_ = mode; }
  [[nodiscard]] ColorMode color_mode() const noexcept { return color_mode_; }

  /// When true and mode is truecolor: emit both FG and BG SGR for cell RGB.
  void set_color_bg(bool enabled) noexcept { color_bg_ = enabled; }
  [[nodiscard]] bool color_bg() const noexcept { return color_bg_; }

  /// Enter presentation mode: hide cursor, optional alt screen.
  VoidResult begin(bool use_alt_screen = true);

  /// Leave presentation mode and restore terminal.
  void end() noexcept;

  [[nodiscard]] bool active() const noexcept { return active_; }

  /// Query current terminal size (ioctl). Falls back to 80×24.
  [[nodiscard]] static Result<TerminalSize> query_size();

  /// Present a full cell grid. First frame may clear; subsequent use home.
  VoidResult present(const CellGrid& grid);

  /// Enter raw terminal mode (non-canonical, no echo) for key reading.
  void enable_raw_mode();
  /// Restore terminal to the state saved by enable_raw_mode().
  void disable_raw_mode() noexcept;

private:
  bool active_ = false;
  bool alt_screen_ = false;
  bool first_frame_ = true;
  ColorMode color_mode_ = ColorMode::mono;
  bool color_bg_ = false;
  std::string write_buf_;
  termios original_termios_{};
  bool raw_mode_active_ = false;
};

/// RAII guard that always restores the terminal.
class PresenterGuard {
public:
  explicit PresenterGuard(Presenter& p, bool alt = true) : presenter_(p) {
    result_ = presenter_.begin(alt);
  }

  ~PresenterGuard() {
    if (result_) {
      presenter_.end();
    }
  }

  [[nodiscard]] VoidResult status() const { return result_; }

private:
  Presenter& presenter_;
  VoidResult result_;
};

} // namespace zola
