#include "zola/logger.hpp"

extern "C" {
#include <libavutil/log.h>
}

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <string>

namespace zola {
namespace {

struct LogState {
  FILE* fp = nullptr;
  bool enabled = false;
  LogLevel level = LogLevel::warn;
  std::filesystem::path log_dir;
};

LogState& state() {
  static LogState s;
  return s;
}

std::string timestamp_now() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[80];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string today_filename() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "zola-%04d-%02d-%02d.log",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

void rotate_logs(const std::filesystem::path& dir) {
  auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * 30);
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    const auto& p = entry.path();
    if (p.extension() != ".log") continue;
    auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) continue;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    if (sctp < cutoff) {
      std::filesystem::remove(p, ec);
    }
  }
}

void av_log_callback(void* /*ctx*/, int level, const char* fmt, va_list args) {
  auto& s = state();
  if (!s.enabled || !s.fp) return;

  if (level > AV_LOG_WARNING) return;

  char msg[1024];
  std::vsnprintf(msg, sizeof(msg), fmt, args);

  std::size_t len = std::strlen(msg);
  while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
    msg[--len] = '\0';
  }

  const char* tag = (level <= AV_LOG_ERROR) ? "ffmpeg-error" : "ffmpeg-warn";
  std::fprintf(s.fp, "%s [%s] %s\n", timestamp_now().c_str(), tag, msg);
  if (level <= AV_LOG_ERROR) {
    std::fflush(s.fp);
  }
}

} // namespace

void Logger::init(bool enabled, LogLevel level) {
  auto& s = state();
  s.enabled = enabled;
  s.level = level;

  if (!enabled) {
    av_log_set_level(AV_LOG_QUIET);
    av_log_set_callback(nullptr);
  } else {
    av_log_set_callback(av_log_callback);
    av_log_set_level(level == LogLevel::error ? AV_LOG_ERROR : AV_LOG_WARNING);
  }

  if (!enabled) return;

  s.log_dir = std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") /
              ".local" / "state" / "zola" / "logs";
  std::error_code ec;
  std::filesystem::create_directories(s.log_dir, ec);
  if (ec) {
    s.enabled = false;
    return;
  }

  rotate_logs(s.log_dir);

  auto path = s.log_dir / today_filename();
  s.fp = std::fopen(path.c_str(), "a");
  if (!s.fp) {
    s.enabled = false;
  }
}

void Logger::warn(const std::string& msg) {
  auto& s = state();
  if (!s.enabled || s.level > LogLevel::warn) return;
  if (!s.fp) return;
  std::fprintf(s.fp, "%s [warn] %s\n", timestamp_now().c_str(), msg.c_str());
}

void Logger::error(const std::string& msg) {
  auto& s = state();
  if (!s.enabled) return;
  if (!s.fp) return;
  std::fprintf(s.fp, "%s [error] %s\n", timestamp_now().c_str(), msg.c_str());
  std::fflush(s.fp);
}

void Logger::shutdown() {
  auto& s = state();
  av_log_set_callback(nullptr);
  av_log_set_level(AV_LOG_INFO);
  if (s.fp) {
    std::fclose(s.fp);
    s.fp = nullptr;
  }
  s.enabled = false;
}

} // namespace zola
