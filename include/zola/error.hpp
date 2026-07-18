#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace zola {

enum class ErrorKind {
  invalid_argument,
  io_failure,
  decode_failure,
  unsupported,
  terminal_failure,
  end_of_stream,
  ffmpeg_error,
};

struct Error {
  ErrorKind kind = ErrorKind::invalid_argument;
  int ffmpeg_code = 0;
  std::string message;

  Error() = default;
  explicit Error(ErrorKind k) : kind(k) {}

  friend bool operator==(const Error& a, const Error& b) {
    return a.kind == b.kind;
  }
};

inline std::string to_string(const Error& e) {
  switch (e.kind) {
  case ErrorKind::invalid_argument:
    return "invalid argument";
  case ErrorKind::io_failure:
    return "I/O failure";
  case ErrorKind::decode_failure:
    return "decode failure";
  case ErrorKind::unsupported:
    return "unsupported";
  case ErrorKind::terminal_failure:
    return "terminal failure";
  case ErrorKind::end_of_stream:
    return "end of stream";
  case ErrorKind::ffmpeg_error:
    return "ffmpeg error " + std::to_string(e.ffmpeg_code) + ": " + e.message;
  }
  return "unknown error";
}

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace zola
