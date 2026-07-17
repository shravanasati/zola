#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace zola {

/// Fixed-capacity SPSC ring of interleaved S16 PCM samples. Thread-safe for one
/// producer and one consumer. Overflow drops the oldest samples so a decoding
/// producer never deadlocks against a real-time audio device.
class PcmRing {
public:
  explicit PcmRing(std::size_t capacity_samples = 0);
  PcmRing(PcmRing&& other) noexcept;
  PcmRing& operator=(PcmRing&& other) noexcept;
  PcmRing(const PcmRing&) = delete;
  PcmRing& operator=(const PcmRing&) = delete;

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  /// Number of samples currently readable.
  [[nodiscard]] std::size_t size() const noexcept;

  /// Number of samples that can be written without dropping.
  [[nodiscard]] std::size_t available() const noexcept;

  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  /// Write interleaved samples. If src is larger than available space, the
  /// oldest samples are discarded to make room (drop-oldest policy).
  void write(std::span<const std::int16_t> src) noexcept;

  /// Read up to dst.size() samples, returning the count actually read.
  std::size_t read(std::span<std::int16_t> dst) noexcept;

  /// Discard count samples from the front (oldest).
  void drop(std::size_t count) noexcept;

  void clear() noexcept;

private:
  std::vector<std::int16_t> buffer_;
  std::size_t capacity_ = 0;
  // read_/write_ are sample counts, not buffer indices; wrap on capacity_.
  std::atomic<std::size_t> read_{0};
  std::atomic<std::size_t> write_{0};
};

} // namespace zola
