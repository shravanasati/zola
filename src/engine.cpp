#include "zola/engine.hpp"

#include "zola/image_source.hpp"
#include "zola/video_source.hpp"

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

VoidResult Engine::show_image(const std::filesystem::path& path) {
  ImageSource source(path);
  if (auto r = source.open(); !r) {
    return r;
  }

  if (auto r = source.next_frame(frame_); !r) {
    return std::unexpected(r.error());
  }

  tone_.apply(frame_);

  std::size_t cols = 0;
  std::size_t rows = 0;
  resolve_output_size(frame_.width(), frame_.height(), cols, rows);
  mapper_.map(frame_, cols, rows, grid_, map_color());

  PresenterGuard guard(presenter_, opts_.alt_screen);
  if (!guard.status()) {
    return guard.status();
  }

  if (auto r = presenter_.present(grid_); !r) {
    return r;
  }

  // Keep still image visible until Enter or Ctrl-C when on a TTY.
  if (isatty(STDIN_FILENO)) {
    SignalGuard signals;
    while (!g_interrupted.load(std::memory_order_relaxed)) {
      // Non-blocking-ish wait: short sleeps so SIGINT is noticed.
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      // Also exit if stdin has a newline (user pressed Enter).
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

VoidResult Engine::play_video(const std::filesystem::path& path) {
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

  SignalGuard signals;
  auto next_deadline = std::chrono::steady_clock::now();
  const bool do_color = map_color();

  for (;;) {
    if (g_interrupted.load(std::memory_order_relaxed)) {
      break;
    }

    auto got = source.next_frame(frame_);
    if (!got) {
      if (got.error() == Error::end_of_stream) {
        break;
      }
      return std::unexpected(got.error());
    }

    tone_.apply(frame_);
    mapper_.map(frame_, cols, rows, grid_, do_color);
    if (auto r = presenter_.present(grid_); !r) {
      return r;
    }

    next_deadline +=
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            frame_duration);
    const auto now = std::chrono::steady_clock::now();
    if (next_deadline > now) {
      std::this_thread::sleep_until(next_deadline);
    } else {
      // Behind schedule: drop time debt so we don't spiral (skip sleep).
      // Optionally snap deadline forward if very late.
      if (now - next_deadline > frame_duration * 3) {
        next_deadline = now;
      }
    }
  }

  return {};
}

} // namespace zola
