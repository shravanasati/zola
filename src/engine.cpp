#include "zola/engine.hpp"

#include "zola/logger.hpp"
#include "zola/video_source.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <sys/select.h>
#include <unistd.h>

namespace zola {
namespace {

std::atomic<bool> g_interrupted{false};

void on_sigint(int) { g_interrupted.store(true, std::memory_order_relaxed); }

struct SignalGuard {
  SignalGuard() {
    g_interrupted.store(false, std::memory_order_relaxed);
    prev_ = std::signal(SIGINT, on_sigint);
  }
  ~SignalGuard() { std::signal(SIGINT, prev_); }
  void (*prev_)(int) = SIG_DFL;
};

} // namespace

Key Engine::read_key() noexcept {
  if (!isatty(STDIN_FILENO)) {
    return Key::none;
  }

  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  timeval tv{0, 0};
  if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) {
    return Key::none;
  }

  unsigned char buf[3] = {};
  const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
  if (n <= 0) {
    return Key::none;
  }

  if (buf[0] == 'q' || buf[0] == 0x1b) {
    if (n == 1) {
      return Key::quit;
    }
    if (buf[1] == '[' && n >= 3) {
      switch (buf[2]) {
      case 'C':
        return Key::seek_fwd;
      case 'D':
        return Key::seek_back;
      default:
        return Key::quit;
      }
    }
    return Key::quit;
  }
  if (buf[0] == ' ' || buf[0] == '\n' || buf[0] == '\r') {
    return Key::space;
  }

  return Key::none;
}

Engine::Engine(EngineOptions opts)
    : opts_(std::move(opts)), tone_(opts_.tone), mapper_(opts_.ramp) {
  if (opts_.invert) {
    invert_ramp_storage_.assign(opts_.ramp.glyphs().rbegin(),
                                opts_.ramp.glyphs().rend());
    mapper_.set_ramp(GlyphRamp(invert_ramp_storage_));
  }
  presenter_.set_color_mode(opts_.color);
  presenter_.set_color_bg(opts_.color_bg);
}

void Engine::resolve_output_size(std::size_t src_w, std::size_t src_h,
                                 std::size_t& cols,
                                 std::size_t& rows) const {
  if (opts_.cols > 0 && opts_.rows > 0) {
    cols = opts_.cols;
    rows = opts_.rows;
    return;
  }

  auto term = Presenter::query_size();
  const std::size_t max_cols =
      opts_.cols > 0 ? opts_.cols : (term ? term->cols : 80);
  const std::size_t max_rows =
      opts_.rows > 0 ? opts_.rows : (term ? term->rows : 24);

  // Leave one row free when using full terminal to reduce scroll glitches.
  const std::size_t fit_rows =
      max_rows > 1 && opts_.rows == 0 ? max_rows - 1 : max_rows;

  mapper_.fit_size(src_w, src_h, max_cols, fit_rows, cols, rows);
}

VoidResult Engine::play(const std::string& path) {
  VideoSource source(path);
  if (auto r = source.open(); !r) {
    return r;
  }

  double fps = opts_.fps_override > 0.0 ? opts_.fps_override : source.fps();
  if (fps <= 0.0) {
    fps = 24.0;
  }
  const auto frame_duration =
      std::chrono::duration<double>(1.0 / fps);

  std::size_t cols = 0;
  std::size_t rows = 0;
  resolve_output_size(source.width(), source.height(), cols, rows);

  PresenterGuard guard(presenter_, opts_.alt_screen);
  if (!guard.status()) {
    return guard.status();
  }

  const bool do_color = map_color();

  // Decode and present the first frame.
  auto got = source.next_frame(frame_);
  if (!got) {
    return std::unexpected(got.error());
  }
  tone_.apply(frame_);
  mapper_.map(frame_, cols, rows, grid_, do_color);
  if (auto r = presenter_.present(grid_); !r) {
    return r;
  }

  // Try a second frame. If EOF, it's a still image.
  auto got2 = source.next_frame(frame_);
  if (!got2 && got2.error().kind == ErrorKind::end_of_stream) {
    if (isatty(STDIN_FILENO)) {
      SignalGuard signals;
      while (!g_interrupted.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{0, 0};
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
          break;
        }
      }
    }
    return {};
  }
  if (!got2) {
    return std::unexpected(got2.error());
  }

  // Video: present the second frame, then loop.
  tone_.apply(frame_);
  mapper_.map(frame_, cols, rows, grid_, do_color);
  if (auto r = presenter_.present(grid_); !r) {
    return r;
  }

  // Audio.
  AudioOutput audio;
  const bool use_audio = source.has_audio() && !opts_.mute;
  if (use_audio) {
    if (auto r = audio.open(source.audio_format()); !r) {
      Logger::warn("audio device unavailable, continuing silent");
    } else {
      audio.start();
    }
  }

  SignalGuard signals;
  auto next_deadline = std::chrono::steady_clock::now();
  const bool audio_clock = audio.is_open();
  const double sample_rate = audio_clock
                                 ? static_cast<double>(source.audio_format().sample_rate)
                                 : 0.0;
  const int channels = audio_clock ? source.audio_format().channels : 0;
  bool paused = false;
  auto pause_start = std::chrono::steady_clock::time_point{};
  auto pause_accumulated = std::chrono::steady_clock::duration{};
  double current_pts = frame_.pts();

  for (;;) {
    if (g_interrupted.load(std::memory_order_relaxed)) {
      break;
    }

    const Key key = read_key();
    if (key == Key::quit) {
      break;
    }
    if (key == Key::space) {
      if (paused) {
        pause_accumulated += std::chrono::steady_clock::now() - pause_start;
        if (audio.is_open()) {
          audio.ring().clear();
          audio.resume();
        }
        next_deadline = std::chrono::steady_clock::now();
        paused = false;
      } else {
        paused = true;
        pause_start = std::chrono::steady_clock::now();
        if (audio.is_open()) {
          audio.pause();
        }
      }
    }
    if (key == Key::seek_fwd || key == Key::seek_back) {
      const double delta = (key == Key::seek_fwd) ? 5.0 : -5.0;
      double target = current_pts + delta;
      const double dur = source.duration();
      if (dur > 0.0) {
        target = std::clamp(target, 0.0, dur);
      } else {
        target = std::max(target, 0.0);
      }
      if (auto sr = source.seek(target); !sr) {
        return std::unexpected(sr.error());
      }
      if (audio_clock) {
        audio.ring().clear();
        const auto target_samples = static_cast<std::size_t>(
            target * sample_rate * channels);
        audio.set_samples_played(target_samples);
      }
      next_deadline = std::chrono::steady_clock::now();
    }

    if (paused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }

    if (use_audio) {
      if (auto pa = source.pump_audio(audio.ring()); !pa) {
      }
    }

    auto got = source.next_frame(frame_);
    if (!got) {
      if (got.error().kind == ErrorKind::end_of_stream) {
        break;
      }
      return std::unexpected(got.error());
    }

    tone_.apply(frame_);
    mapper_.map(frame_, cols, rows, grid_, do_color);
    if (auto r = presenter_.present(grid_); !r) {
      return r;
    }
    current_pts = frame_.pts();

    double clock_seconds = 0.0;
    if (audio_clock && sample_rate > 0.0 && channels > 0) {
      clock_seconds =
          static_cast<double>(audio.samples_played()) / (sample_rate * channels);
    } else {
      const auto effective_now =
          std::chrono::steady_clock::now() - pause_accumulated;
      clock_seconds =
          std::chrono::duration<double>(effective_now.time_since_epoch()).count();
    }

    const double pts_seconds = frame_.pts();
    if (audio_clock && pts_seconds > 0.0) {
      const double ahead = pts_seconds - clock_seconds;
      if (ahead > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(ahead));
      } else if (-ahead > 2.0 / fps) {
        next_deadline = std::chrono::steady_clock::now();
      }
    } else {
      next_deadline +=
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              frame_duration);
      const auto now = std::chrono::steady_clock::now();
      if (next_deadline > now) {
        std::this_thread::sleep_until(next_deadline);
      } else {
        if (now - next_deadline > frame_duration * 3) {
          next_deadline = now;
        }
      }
    }
  }

  audio.stop();
  return {};
}

} // namespace zola
