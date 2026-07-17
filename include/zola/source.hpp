#pragma once

#include "zola/error.hpp"
#include "zola/frame.hpp"

#include <cstddef>

namespace zola {

/// Produces a sequence of Frames. Image sources yield one frame; video many.
class Source {
public:
  virtual ~Source() = default;

  /// Open/load the underlying media. Must succeed before next_frame.
  virtual VoidResult open() = 0;

  /// Decode the next frame into `out` (reuses buffer when sized correctly).
  /// Returns Error::end_of_stream when finished.
  virtual Result<bool> next_frame(Frame& out) = 0;

  [[nodiscard]] virtual std::size_t width() const noexcept = 0;
  [[nodiscard]] virtual std::size_t height() const noexcept = 0;

  /// Frames per second when known (0 if still / unknown).
  [[nodiscard]] virtual double fps() const noexcept { return 0.0; }
};

} // namespace zola
