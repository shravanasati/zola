#include "zola/engine.hpp"
#include "zola/error.hpp"
#include "zola/logger.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage(const char* argv0) {
  std::cerr
      << "zola — high-performance ASCII media engine\n\n"
      << "Usage:\n"
      << "  " << argv0
      << " image <path> [options]\n"
      << "  " << argv0
      << " play  <path> [options]\n"
      << "  " << argv0 << " --help\n\n"
      << "Options:\n"
      << "  --cols N          Output columns (default: fit terminal)\n"
      << "  --rows N          Output rows (default: fit terminal)\n"
      << "  --fps N           Force playback FPS (video only)\n"
      << "  --ramp STR        Glyph ramp dark→light (default: \" .:-=+*#%@\")\n"
      << "  --no-alt          Do not use the alternate screen buffer\n"
      << "  --color           Enable truecolor SGR (same as --color=truecolor)\n"
      << "  --color=MODE      mono | truecolor (default: mono)\n"
      << "  --bg              Color background (48;2) as well as foreground\n"
      << "  --brightness F    Additive bias after contrast (default: 0)\n"
      << "  --contrast F      Gain around mid-gray (default: 1)\n"
      << "  --gamma F         Power curve on luminance (default: 1)\n"
      << "  --auto-levels     Stretch 1st–99th percentiles to full range\n"
      << "  --invert          Reverse glyph ramp (light backgrounds)\n"
      << "  --mute            Disable audio output (video still plays)\n"
      << "  --volume F        Linear volume 0..1 (default: 1)\n"
      << "  --log-level LVL   Log level: warn, error (default: warn)\n";
}

struct Parsed {
  enum class Cmd { none, image, play } cmd = Cmd::none;
  std::string path;
  zola::EngineOptions opts;
  bool help = false;
  bool ok = true;
  std::string error;
  zola::LogLevel log_level = zola::LogLevel::warn;
};

bool parse_color_mode(std::string_view s, zola::ColorMode& out) {
  if (s == "mono") {
    out = zola::ColorMode::mono;
    return true;
  }
  if (s == "truecolor") {
    out = zola::ColorMode::truecolor;
    return true;
  }
  return false;
}

Parsed parse_args(int argc, char** argv) {
  Parsed p;
  if (argc < 2) {
    p.ok = false;
    p.error = "missing command";
    return p;
  }

  int i = 1;
  const std::string_view a1 = argv[1];
  if (a1 == "--help" || a1 == "-h") {
    p.help = true;
    return p;
  }
  if (a1 == "image") {
    p.cmd = Parsed::Cmd::image;
  } else if (a1 == "play") {
    p.cmd = Parsed::Cmd::play;
  } else {
    p.ok = false;
    p.error = "unknown command (use image or play)";
    return p;
  }

  if (argc < 3) {
    p.ok = false;
    p.error = "missing path";
    return p;
  }
  p.path = argv[2];
  i = 3;

  auto need_value = [&](const char* flag) -> const char* {
    if (i + 1 >= argc) {
      p.ok = false;
      p.error = std::string("missing value for ") + flag;
      return nullptr;
    }
    return argv[++i];
  };

  // Keep ramp storage alive for the process lifetime via static / opts copy.
  static std::string ramp_storage;

  for (; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--cols") {
      const char* v = need_value("--cols");
      if (!v) {
        return p;
      }
      p.opts.cols = static_cast<std::size_t>(std::strtoul(v, nullptr, 10));
    } else if (a == "--rows") {
      const char* v = need_value("--rows");
      if (!v) {
        return p;
      }
      p.opts.rows = static_cast<std::size_t>(std::strtoul(v, nullptr, 10));
    } else if (a == "--fps") {
      const char* v = need_value("--fps");
      if (!v) {
        return p;
      }
      p.opts.fps_override = std::strtod(v, nullptr);
    } else if (a == "--ramp") {
      const char* v = need_value("--ramp");
      if (!v) {
        return p;
      }
      ramp_storage = v;
      p.opts.ramp = zola::GlyphRamp(ramp_storage);
    } else if (a == "--no-alt") {
      p.opts.alt_screen = false;
    } else if (a == "--color") {
      p.opts.color = zola::ColorMode::truecolor;
    } else if (a.starts_with("--color=")) {
      const std::string_view mode = a.substr(sizeof("--color=") - 1);
      if (!parse_color_mode(mode, p.opts.color)) {
        p.ok = false;
        p.error = std::string("unknown --color mode: ") + std::string(mode) +
                  " (use mono or truecolor)";
        return p;
      }
    } else if (a == "--bg") {
      p.opts.color_bg = true;
    } else if (a == "--brightness") {
      const char* v = need_value("--brightness");
      if (!v) {
        return p;
      }
      p.opts.tone.brightness = std::strtod(v, nullptr);
    } else if (a == "--contrast") {
      const char* v = need_value("--contrast");
      if (!v) {
        return p;
      }
      p.opts.tone.contrast = std::strtod(v, nullptr);
    } else if (a == "--gamma") {
      const char* v = need_value("--gamma");
      if (!v) {
        return p;
      }
      p.opts.tone.gamma = std::strtod(v, nullptr);
    } else if (a == "--auto-levels") {
      p.opts.tone.auto_levels = true;
    } else if (a == "--invert") {
      p.opts.invert = true;
    } else if (a == "--mute") {
      p.opts.mute = true;
    } else if (a == "--log-level") {
      const char* v = need_value("--log-level");
      if (!v) {
        return p;
      }
      if (std::string_view(v) == "warn") {
        p.log_level = zola::LogLevel::warn;
      } else if (std::string_view(v) == "error") {
        p.log_level = zola::LogLevel::error;
      } else {
        p.ok = false;
        p.error = std::string("unknown --log-level: ") + v + " (use warn or error)";
        return p;
      }
    } else if (a == "--volume") {
      const char* v = need_value("--volume");
      if (!v) {
        return p;
      }
      p.opts.volume = static_cast<float>(std::strtod(v, nullptr));
      if (p.opts.volume < 0.0f || p.opts.volume > 1.0f) {
        p.ok = false;
        p.error = "--volume must be between 0 and 1";
        return p;
      }
    } else if (a == "--help" || a == "-h") {
      p.help = true;
      return p;
    } else {
      p.ok = false;
      p.error = std::string("unknown option: ") + argv[i];
      return p;
    }
  }

  return p;
}

int fail(zola::Error e) {
  std::cerr << "zola: " << zola::to_string(e) << '\n';
  return 1;
}

} // namespace

int main(int argc, char** argv) {
  const Parsed p = parse_args(argc, argv);
  if (p.help) {
    print_usage(argv[0]);
    return 0;
  }
  if (!p.ok) {
    std::cerr << "zola: " << p.error << "\n\n";
    print_usage(argv[0]);
    return 2;
  }

  zola::Logger::init(true, p.log_level);

  zola::Engine engine(p.opts);
  zola::VoidResult result;

  switch (p.cmd) {
  case Parsed::Cmd::image:
    result = engine.show_image(p.path);
    break;
  case Parsed::Cmd::play:
    result = engine.play_video(p.path);
    break;
  default:
    print_usage(argv[0]);
    return 2;
  }

  if (!result) {
    zola::Logger::error(zola::to_string(result.error()));
    zola::Logger::shutdown();
    return fail(result.error());
  }

  zola::Logger::shutdown();
  return 0;
}
