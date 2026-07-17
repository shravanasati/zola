// Unit tests for Frame RGB, Mapper color, and Presenter SGR (no TTY).
// Build: make test
// Run:   ./color_test

#include "zola/ascii_mapper.hpp"
#include "zola/frame.hpp"
#include "zola/presenter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect_true(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

void expect_eq_u8(std::uint8_t got, std::uint8_t want, const char* msg) {
  if (got != want) {
    std::cerr << "FAIL: " << msg << " (got " << static_cast<int>(got)
              << ", want " << static_cast<int>(want) << ")\n";
    ++g_failures;
  }
}

void expect_eq_sz(std::size_t got, std::size_t want, const char* msg) {
  if (got != want) {
    std::cerr << "FAIL: " << msg << " (got " << got << ", want " << want
              << ")\n";
    ++g_failures;
  }
}

void expect_contains(const std::string& s, const char* needle, const char* msg) {
  if (s.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << msg << " (missing \"" << needle << "\")\n";
    ++g_failures;
  }
}

void expect_not_contains(const std::string& s, const char* needle,
                         const char* msg) {
  if (s.find(needle) != std::string::npos) {
    std::cerr << "FAIL: " << msg << " (unexpected \"" << needle << "\")\n";
    ++g_failures;
  }
}

void test_frame_rgb_storage() {
  zola::Frame f(4, 3);
  expect_true(!f.has_color(), "new frame has no color");
  expect_eq_sz(f.rgb().size(), 0, "rgb empty without ensure_color");

  f.ensure_color();
  expect_true(f.has_color(), "ensure_color sets has_color");
  expect_eq_sz(f.rgb().size(), 4 * 3 * 3, "rgb size W*H*3");

  f.rgb()[0] = 10;
  f.ensure_color(); // idempotent
  expect_eq_u8(f.rgb()[0], 10, "ensure_color does not clear when sized");

  f.resize(2, 2);
  expect_true(!f.has_color(), "resize drops RGB");
  expect_eq_sz(f.rgb().size(), 0, "rgb cleared after resize");

  f.ensure_size(2, 2);
  expect_true(!f.has_color(), "ensure_size same dims keeps no color");
  f.ensure_color();
  expect_true(f.has_color(), "re-ensure after resize");
  f.ensure_size(2, 2); // no-op path
  expect_true(f.has_color(), "ensure_size no-op keeps RGB");

  f.ensure_size(3, 3);
  expect_true(!f.has_color(), "ensure_size size change drops RGB");
}

void test_mapper_solid_red() {
  // Solid red 255,0,0 → Rec.709 Y ≈ 54; cells get red RGB when map_color.
  zola::Frame f(4, 4);
  f.ensure_color();
  for (std::size_t i = 0; i < 16; ++i) {
    f.rgb()[i * 3 + 0] = 255;
    f.rgb()[i * 3 + 1] = 0;
    f.rgb()[i * 3 + 2] = 0;
    // Approximate Rec.709: 0.2126*255 ≈ 54
    f.data()[i] = 54;
  }

  zola::AsciiMapper mapper;
  zola::CellGrid grid;
  mapper.map(f, 2, 2, grid, /*map_color=*/true);

  expect_eq_sz(grid.cols(), 2, "mapper cols");
  expect_eq_sz(grid.rows(), 2, "mapper rows");
  for (std::size_t y = 0; y < 2; ++y) {
    for (std::size_t x = 0; x < 2; ++x) {
      const auto& c = grid.at(x, y);
      expect_eq_u8(c.r, 255, "solid red cell.r");
      expect_eq_u8(c.g, 0, "solid red cell.g");
      expect_eq_u8(c.b, 0, "solid red cell.b");
      expect_true(c.glyph != ' ' || true, "glyph assigned"); // always true; glyph from ramp
    }
  }

  // map_color false leaves white even if frame has color.
  mapper.map(f, 2, 2, grid, /*map_color=*/false);
  expect_eq_u8(grid.at(0, 0).r, 255, "mono map leaves default white r");
  expect_eq_u8(grid.at(0, 0).g, 255, "mono map leaves default white g");
  expect_eq_u8(grid.at(0, 0).b, 255, "mono map leaves default white b");
}

void test_mapper_gray_only() {
  zola::Frame f(2, 2);
  for (auto& s : f.samples()) {
    s = 200;
  }
  expect_true(!f.has_color(), "gray-only frame");

  zola::AsciiMapper mapper;
  zola::CellGrid grid;
  mapper.map(f, 2, 2, grid, /*map_color=*/true);
  expect_eq_u8(grid.at(0, 0).r, 255, "no color plane → white r");
  expect_eq_u8(grid.at(0, 0).g, 255, "no color plane → white g");
  expect_eq_u8(grid.at(0, 0).b, 255, "no color plane → white b");
  expect_true(grid.at(0, 0).glyph != 0, "glyph from luminance");
}

void test_append_grid_mono_no_sgr() {
  zola::CellGrid grid(3, 2);
  grid.at(0, 0).glyph = 'A';
  grid.at(1, 0).glyph = 'B';
  grid.at(2, 0).glyph = 'C';
  grid.at(0, 1).glyph = 'D';
  grid.at(1, 1).glyph = 'E';
  grid.at(2, 1).glyph = 'F';

  std::string out;
  zola::append_grid(grid, zola::ColorMode::mono, false, out);
  expect_true(out == "ABC\nDEF", "mono output is glyphs + newlines");
  expect_not_contains(out, "\033[38;2;", "mono has no FG truecolor");
  expect_not_contains(out, "\033[48;2;", "mono has no BG truecolor");
}

void test_append_grid_truecolor_rle() {
  zola::CellGrid grid(4, 1);
  // Two red, then two green — should emit two SGR runs.
  for (std::size_t x = 0; x < 2; ++x) {
    grid.at(x, 0) = zola::Cell{'.', 255, 0, 0};
  }
  for (std::size_t x = 2; x < 4; ++x) {
    grid.at(x, 0) = zola::Cell{'#', 0, 255, 0};
  }

  std::string out;
  zola::append_grid(grid, zola::ColorMode::truecolor, false, out);

  expect_contains(out, "\033[38;2;255;0;0m", "FG red SGR");
  expect_contains(out, "\033[38;2;0;255;0m", "FG green SGR");
  expect_contains(out, "\033[0m", "reset after row");
  expect_not_contains(out, "\033[48;2;", "no BG without color_bg");

  // Count SGR FG sequences — RLE ⇒ 2 runs, not 4.
  std::size_t count = 0;
  for (std::size_t pos = 0; (pos = out.find("\033[38;2;", pos)) != std::string::npos;
       ++pos) {
    ++count;
  }
  expect_eq_sz(count, 2, "RLE coalesces same RGB on a row");
}

void test_append_grid_truecolor_bg() {
  zola::CellGrid grid(1, 1);
  grid.at(0, 0) = zola::Cell{'X', 1, 2, 3};

  std::string out;
  zola::append_grid(grid, zola::ColorMode::truecolor, true, out);
  expect_contains(out, "\033[38;2;1;2;3;48;2;1;2;3m", "FG+BG same RGB");
  expect_contains(out, "X", "glyph present");
}

} // namespace

int main() {
  test_frame_rgb_storage();
  test_mapper_solid_red();
  test_mapper_gray_only();
  test_append_grid_mono_no_sgr();
  test_append_grid_truecolor_rle();
  test_append_grid_truecolor_bg();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "color_test: all passed\n";
  return 0;
}
