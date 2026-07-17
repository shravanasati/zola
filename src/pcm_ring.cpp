#include "zola/pcm_ring.hpp"

#include <algorithm>
#include <cstring>

namespace zola {

PcmRing::PcmRing(std::size_t capacity_samples)
    : buffer_(capacity_samples), capacity_(capacity_samples) {}

PcmRing::PcmRing(PcmRing&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      capacity_(other.capacity_),
      read_(other.read_.load(std::memory_order_relaxed)),
      write_(other.write_.load(std::memory_order_relaxed)) {
  other.capacity_ = 0;
  other.read_.store(0, std::memory_order_relaxed);
  other.write_.store(0, std::memory_order_relaxed);
}

PcmRing& PcmRing::operator=(PcmRing&& other) noexcept {
  if (this != &other) {
    buffer_ = std::move(other.buffer_);
    capacity_ = other.capacity_;
    read_.store(other.read_.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
    write_.store(other.write_.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
    other.capacity_ = 0;
    other.read_.store(0, std::memory_order_relaxed);
    other.write_.store(0, std::memory_order_relaxed);
  }
  return *this;
}

std::size_t PcmRing::size() const noexcept {
  const std::size_t r = read_.load(std::memory_order_acquire);
  const std::size_t w = write_.load(std::memory_order_acquire);
  return w - r;
}

std::size_t PcmRing::available() const noexcept {
  const std::size_t s = size();
  return capacity_ > s ? capacity_ - s : 0;
}

void PcmRing::write(std::span<const std::int16_t> src) noexcept {
  if (capacity_ == 0 || src.empty()) {
    return;
  }

  std::size_t r = read_.load(std::memory_order_relaxed);
  std::size_t w = write_.load(std::memory_order_relaxed);
  std::size_t used = w - r;

  // Drop oldest samples if the write would overflow.
  if (src.size() > capacity_ - used) {
    const std::size_t drop = src.size() - (capacity_ - used);
    r += drop;
    used = w - r;
  }

  const std::size_t write_idx = w % capacity_;
  const std::size_t contiguous = std::min(capacity_ - write_idx, src.size());
  const std::size_t wrapped = src.size() - contiguous;

  std::memcpy(buffer_.data() + write_idx, src.data(),
              contiguous * sizeof(std::int16_t));
  if (wrapped > 0) {
    std::memcpy(buffer_.data(), src.data() + contiguous,
                wrapped * sizeof(std::int16_t));
  }

  write_.store(w + src.size(), std::memory_order_release);
  read_.store(r, std::memory_order_release);
}

std::size_t PcmRing::read(std::span<std::int16_t> dst) noexcept {
  if (capacity_ == 0 || dst.empty()) {
    return 0;
  }

  const std::size_t r = read_.load(std::memory_order_relaxed);
  const std::size_t w = write_.load(std::memory_order_acquire);
  const std::size_t used = w - r;
  const std::size_t n = std::min(dst.size(), used);
  if (n == 0) {
    return 0;
  }

  const std::size_t read_idx = r % capacity_;
  const std::size_t contiguous = std::min(capacity_ - read_idx, n);
  const std::size_t wrapped = n - contiguous;

  std::memcpy(dst.data(), buffer_.data() + read_idx,
              contiguous * sizeof(std::int16_t));
  if (wrapped > 0) {
    std::memcpy(dst.data() + contiguous, buffer_.data(),
                wrapped * sizeof(std::int16_t));
  }

  read_.store(r + n, std::memory_order_release);
  return n;
}

void PcmRing::drop(std::size_t count) noexcept {
  const std::size_t r = read_.load(std::memory_order_relaxed);
  const std::size_t w = write_.load(std::memory_order_acquire);
  const std::size_t used = w - r;
  read_.store(r + std::min(count, used), std::memory_order_release);
}

void PcmRing::clear() noexcept {
  read_.store(0, std::memory_order_release);
  write_.store(0, std::memory_order_release);
}

} // namespace zola
