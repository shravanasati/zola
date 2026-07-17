// Unit tests for ToneMap (no TTY required).
// Build: make test
// Run:   ./tone_map_test

#include "zola/frame.hpp"
#include "zola/tone_map.hpp"

#include <cmath>
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

void expect_near_u8(std::uint8_t got, std::uint8_t want, int tol,
                    const char* msg) {
  const int d = std::abs(static_cast<int>(got) - static_cast<int>(want));
  if (d > tol) {
    std::cerr << "FAIL: " << msg << " (got " << static_cast<int>(got)
              << ", want " << static_cast<int>(want) << " ±" << tol << ")\n";
    ++g_failures;
  }
}

zola::Frame make_flat(std::size_t w, std::size_t h, std::uint8_t y) {
  zola::Frame f(w, h);
  for (auto& s : f.samples()) {
    s = y;
  }
  return f;
}

void test_identity() {
  zola::ToneMap tm;
  expect_true(tm.is_identity(), "default ToneMap is identity");

  auto f = make_flat(4, 4, 128);
  tm.apply(f);
  for (auto s : f.samples()) {
    expect_eq_u8(s, 128, "identity leaves mid-gray");
  }
}

void test_brightness_up() {
  zola::ToneMapParams p;
  p.brightness = 0.2;
  zola::ToneMap tm(p);
  expect_true(!tm.is_identity(), "brightness != 0 is not identity");

  // Mid-gray 128 → t=0.5 → 0.5+0.2=0.7 → ~179
  expect_near_u8(tm.map_sample(128, 0, 255), 179, 1, "brightness +0.2 mid");
  expect_eq_u8(tm.map_sample(255, 0, 255), 255, "brightness clamps high");
  expect_near_u8(tm.map_sample(0, 0, 255), 51, 1, "brightness +0.2 black");

  auto f = make_flat(2, 2, 100);
  tm.apply(f);
  for (auto s : f.samples()) {
    expect_true(s > 100, "frame brightness raises samples");
  }
}

void test_brightness_down() {
  zola::ToneMapParams p;
  p.brightness = -0.25;
  zola::ToneMap tm(p);
  expect_near_u8(tm.map_sample(128, 0, 255), 64, 1, "brightness -0.25 mid");
  expect_eq_u8(tm.map_sample(0, 0, 255), 0, "brightness clamps low");
}

void test_contrast() {
  zola::ToneMapParams p;
  p.contrast = 1.5;
  zola::ToneMap tm(p);

  // t=0.5 stays mid; t=0 → (0-0.5)*1.5+0.5 = -0.25 → 0
  expect_eq_u8(tm.map_sample(128, 0, 255), 128, "contrast mid fixed");
  expect_eq_u8(tm.map_sample(0, 0, 255), 0, "contrast clamps black");
  expect_eq_u8(tm.map_sample(255, 0, 255), 255, "contrast clamps white");

  // t = 0.25 → (0.25-0.5)*1.5+0.5 = 0.125 → ~32
  expect_near_u8(tm.map_sample(64, 0, 255), 32, 1, "contrast 1.5 on 64");
  // t = 0.75 → (0.75-0.5)*1.5+0.5 = 0.875 → ~223
  expect_near_u8(tm.map_sample(191, 0, 255), 223, 1, "contrast 1.5 on 191");
}

void test_gamma() {
  zola::ToneMapParams p;
  p.gamma = 0.5; // lifts midtones
  zola::ToneMap tm(p);

  // t=0.25 → sqrt(0.25)=0.5 → 128
  expect_near_u8(tm.map_sample(64, 0, 255), 128, 1, "gamma 0.5 lifts 64");
  expect_eq_u8(tm.map_sample(0, 0, 255), 0, "gamma keeps black");
  expect_eq_u8(tm.map_sample(255, 0, 255), 255, "gamma keeps white");
}

void test_auto_levels() {
  zola::Frame f(10, 10);
  // Fill with values in a narrow band [50, 150].
  std::size_t i = 0;
  for (auto& s : f.samples()) {
    s = static_cast<std::uint8_t>(50 + (i % 101));
    ++i;
  }

  zola::ToneMapParams p;
  p.auto_levels = true;
  p.low_percentile = 0.0;
  p.high_percentile = 1.0;
  zola::ToneMap tm(p);
  tm.apply(f);

  std::uint8_t min_v = 255;
  std::uint8_t max_v = 0;
  for (auto s : f.samples()) {
    min_v = std::min(min_v, s);
    max_v = std::max(max_v, s);
  }
  expect_true(min_v <= 2, "auto-levels stretches low near 0");
  expect_true(max_v >= 253, "auto-levels stretches high near 255");
}

void test_combined_order() {
  // gamma then contrast then brightness (documented order).
  zola::ToneMapParams p;
  p.gamma = 2.0;
  p.contrast = 2.0;
  p.brightness = 0.1;
  zola::ToneMap tm(p);

  // y=128 → t=0.5 → pow 2 → 0.25 → (0.25-0.5)*2+0.5 = 0 → +0.1 → 0.1 → ~26
  expect_near_u8(tm.map_sample(128, 0, 255), 26, 1, "combined mid sample");
}

void test_copy_apply() {
  zola::ToneMapParams p;
  p.brightness = 0.1;
  zola::ToneMap tm(p);

  auto in = make_flat(3, 3, 100);
  zola::Frame out;
  tm.apply(in, out);

  expect_eq_u8(in.at(0, 0), 100, "copy apply leaves input");
  expect_true(out.width() == 3 && out.height() == 3, "out size matches");
  expect_true(out.at(0, 0) > 100, "out is brighter");
}

} // namespace

int main() {
  test_identity();
  test_brightness_up();
  test_brightness_down();
  test_contrast();
  test_gamma();
  test_auto_levels();
  test_combined_order();
  test_copy_apply();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "all tone_map tests passed\n";
  return 0;
}
