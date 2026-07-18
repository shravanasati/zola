#include "zola/presenter.hpp"

#include <cstdio>
#include <sys/ioctl.h>
#include <unistd.h>

namespace zola {
namespace {

void append_u8(std::string& out, std::uint8_t v) {
  // Decimal without locale / heap; max 3 digits.
  if (v >= 100) {
    out.push_back(static_cast<char>('0' + v / 100));
    out.push_back(static_cast<char>('0' + (v / 10) % 10));
    out.push_back(static_cast<char>('0' + v % 10));
  } else if (v >= 10) {
    out.push_back(static_cast<char>('0' + v / 10));
    out.push_back(static_cast<char>('0' + v % 10));
  } else {
    out.push_back(static_cast<char>('0' + v));
  }
}

void append_truecolor_sgr(std::string& out, std::uint8_t r, std::uint8_t g,
                          std::uint8_t b, bool color_bg) {
  // FG always; with --bg also set BG to the same RGB.
  out.append("\033[38;2;");
  append_u8(out, r);
  out.push_back(';');
  append_u8(out, g);
  out.push_back(';');
  append_u8(out, b);
  if (color_bg) {
    out.append(";48;2;");
    append_u8(out, r);
    out.push_back(';');
    append_u8(out, g);
    out.push_back(';');
    append_u8(out, b);
  }
  out.push_back('m');
}

bool same_rgb(const Cell& a, const Cell& b) noexcept {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

} // namespace

void append_grid(const CellGrid& grid, ColorMode mode, bool color_bg,
                 std::string& out) {
  const std::size_t cols = grid.cols();
  const std::size_t rows = grid.rows();
  if (cols == 0 || rows == 0) {
    return;
  }

  if (mode == ColorMode::mono) {
    for (std::size_t y = 0; y < rows; ++y) {
      for (std::size_t x = 0; x < cols; ++x) {
        out.push_back(grid.at(x, y).glyph);
      }
      if (y + 1 < rows) {
        out.push_back('\n');
      }
    }
    return;
  }

  // truecolor: RLE consecutive equal RGB on a row; reset once per row end.
  for (std::size_t y = 0; y < rows; ++y) {
    std::size_t x = 0;
    while (x < cols) {
      const Cell& head = grid.at(x, y);
      std::size_t run_end = x + 1;
      while (run_end < cols && same_rgb(grid.at(run_end, y), head)) {
        ++run_end;
      }
      append_truecolor_sgr(out, head.r, head.g, head.b, color_bg);
      for (std::size_t i = x; i < run_end; ++i) {
        out.push_back(grid.at(i, y).glyph);
      }
      x = run_end;
    }
    out.append("\033[0m");
    if (y + 1 < rows) {
      out.push_back('\n');
    }
  }
}

Presenter::~Presenter() { end(); }

VoidResult Presenter::begin(bool use_alt_screen) {
  if (active_) {
    return {};
  }
  if (!isatty(STDOUT_FILENO)) {
    // Allow non-TTY for piping/tests; still write ANSI.
  }

  alt_screen_ = use_alt_screen;
  if (alt_screen_) {
    // Enter alternate screen buffer.
    std::fputs("\033[?1049h", stdout);
  }
  // Hide cursor, clear screen, home.
  std::fputs("\033[?25l\033[2J\033[H", stdout);
  std::fflush(stdout);

  active_ = true;
  first_frame_ = true;
  return {};
}

void Presenter::end() noexcept {
  if (!active_) {
    return;
  }
  // Show cursor; leave alt screen if entered.
  std::fputs("\033[?25h", stdout);
  if (alt_screen_) {
    std::fputs("\033[?1049l", stdout);
  }
  std::fflush(stdout);
  active_ = false;
  alt_screen_ = false;
}

Result<TerminalSize> Presenter::query_size() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    return TerminalSize{ws.ws_col, ws.ws_row};
  }
  return TerminalSize{80, 24};
}

VoidResult Presenter::present(const CellGrid& grid) {
  if (grid.empty()) {
    return std::unexpected(Error(ErrorKind::invalid_argument));
  }

  const std::size_t cols = grid.cols();
  const std::size_t rows = grid.rows();

  // Upper bound: worst case unique color per cell.
  // SGR FG ~19 bytes; FG+BG ~38; glyph 1; reset ~4; newline 1.
  const std::size_t sgr_per_cell =
      color_mode_ == ColorMode::truecolor ? (color_bg_ ? 40u : 20u) : 0u;
  const std::size_t upper =
      8 + rows * (cols * (1 + sgr_per_cell) + 8);

  write_buf_.clear();
  if (write_buf_.capacity() < upper) {
    write_buf_.reserve(upper);
  }

  if (!first_frame_) {
    write_buf_.append("\033[H");
  } else {
    first_frame_ = false;
  }

  append_grid(grid, color_mode_, color_bg_, write_buf_);

  const auto n = static_cast<std::size_t>(
      std::fwrite(write_buf_.data(), 1, write_buf_.size(), stdout));
  if (n != write_buf_.size()) {
    return std::unexpected(Error(ErrorKind::io_failure));
  }
  if (std::fflush(stdout) != 0) {
    return std::unexpected(Error(ErrorKind::io_failure));
  }
  return {};
}

} // namespace zola
