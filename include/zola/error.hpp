#pragma once

#include <expected>
#include <string_view>

namespace zola {

enum class Error {
  invalid_argument,
  io_failure,
  decode_failure,
  unsupported,
  terminal_failure,
  end_of_stream,
};

inline std::string_view to_string(Error e) {
  switch (e) {
  case Error::invalid_argument:
    return "invalid argument";
  case Error::io_failure:
    return "I/O failure";
  case Error::decode_failure:
    return "decode failure";
  case Error::unsupported:
    return "unsupported";
  case Error::terminal_failure:
    return "terminal failure";
  case Error::end_of_stream:
    return "end of stream";
  }
  return "unknown error";
}

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace zola
