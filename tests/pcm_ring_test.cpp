// Unit tests for PcmRing: wrap, partial fills, overflow drop-oldest.
// Build: make test
// Run:   ./pcm_ring_test

#include "zola/pcm_ring.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
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

void test_basic_write_read() {
  zola::PcmRing ring(8);
  expect_eq_sz(ring.capacity(), 8, "capacity");
  expect_eq_sz(ring.size(), 0, "empty size");

  const std::vector<std::int16_t> in = {1, 2, 3, 4};
  ring.write(std::span(in));
  expect_eq_sz(ring.size(), 4, "size after write");

  std::vector<std::int16_t> out(4, 0);
  const std::size_t n = ring.read(std::span(out));
  expect_eq_sz(n, 4, "read count");
  expect_eq_sz(ring.size(), 0, "size after read");
  for (std::size_t i = 0; i < 4; ++i) {
    expect_true(out[i] == static_cast<std::int16_t>(i + 1), "sample value");
  }
}

void test_wraparound() {
  zola::PcmRing ring(6);
  const std::vector<std::int16_t> a = {1, 2, 3, 4, 5};
  ring.write(std::span(a)); // fill most
  std::vector<std::int16_t> tmp(3, 0);
  expect_eq_sz(ring.read(std::span(tmp)), 3, "read first 3");
  expect_eq_sz(ring.size(), 2, "remaining");

  const std::vector<std::int16_t> b = {6, 7, 8, 9};
  ring.write(std::span(b)); // wraps around: 4,5,6,7,8,9
  expect_eq_sz(ring.size(), 6, "full after wrap write");

  std::vector<std::int16_t> out(6, 0);
  expect_eq_sz(ring.read(std::span(out)), 6, "read all");
  expect_true(out[0] == 4 && out[1] == 5 && out[2] == 6 && out[3] == 7 &&
                  out[4] == 8 && out[5] == 9,
              "wraparound order");
}

void test_overflow_drop_oldest() {
  zola::PcmRing ring(4);
  const std::vector<std::int16_t> a = {1, 2, 3, 4};
  ring.write(std::span(a));
  const std::vector<std::int16_t> b = {5, 6};
  ring.write(std::span(b)); // drops 1,2 -> 3,4,5,6
  expect_eq_sz(ring.size(), 4, "size after overflow");

  std::vector<std::int16_t> out(4, 0);
  expect_eq_sz(ring.read(std::span(out)), 4, "read after overflow");
  expect_true(out[0] == 3 && out[1] == 4 && out[2] == 5 && out[3] == 6,
              "drop oldest");
}

void test_partial_read() {
  zola::PcmRing ring(8);
  const std::vector<std::int16_t> a = {1, 2, 3};
  ring.write(std::span(a));
  std::vector<std::int16_t> out(8, 0);
  expect_eq_sz(ring.read(std::span(out)), 3, "read only available");
  expect_eq_sz(ring.size(), 0, "empty after partial read");
}

void test_drop_and_clear() {
  zola::PcmRing ring(8);
  const std::vector<std::int16_t> a = {1, 2, 3, 4, 5};
  ring.write(std::span(a));
  ring.drop(2);
  expect_eq_sz(ring.size(), 3, "size after drop");
  ring.clear();
  expect_eq_sz(ring.size(), 0, "size after clear");
}

} // namespace

int main() {
  test_basic_write_read();
  test_wraparound();
  test_overflow_drop_oldest();
  test_partial_read();
  test_drop_and_clear();

  if (g_failures != 0) {
    std::cerr << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "pcm_ring_test: all passed\n";
  return 0;
}
