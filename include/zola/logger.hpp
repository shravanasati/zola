#pragma once

#include <string>

namespace zola {

enum class LogLevel { warn, error };

class Logger {
public:
  static void init(bool enabled, LogLevel level);
  static void warn(const std::string& msg);
  static void error(const std::string& msg);
  static void shutdown();
};

} // namespace zola
