# Zola

High-performance **ASCII media engine** in modern C++. Render images and play videos as ASCII art in your terminal — mono by default, optional truecolor.

## Requirements

- C++23 compiler (GCC 12+ or Clang 16+)
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libswscale`, `libavutil`
- Linux / POSIX terminal

### Fedora / RHEL

```bash
sudo dnf install gcc-c++ ffmpeg-free-devel
# or full FFmpeg from RPM Fusion if preferred
```

### Debian / Ubuntu

```bash
sudo apt install g++ libavformat-dev libavcodec-dev libswscale-dev libavutil-dev
```

## Build

```bash
make
./zola --help
```

## Usage

```bash
# Still image → ASCII (fills the terminal by default)
./zola image path/to/photo.jpg

# Silent video playback as ASCII
./zola play path/to/clip.mp4

# Optional sizing
./zola image photo.png --cols 120 --rows 40
./zola play clip.mp4 --fps 24

# Brightness / tone (affects glyph density via luminance)
./zola image photo.png --brightness 0.2
./zola image photo.png --contrast 1.5 --gamma 0.8
./zola play clip.mp4 --auto-levels
./zola image photo.png --invert   # light terminal backgrounds

# Truecolor (24-bit SGR); default remains pure mono ASCII
./zola image photo.png --color
./zola image photo.png --color=truecolor --bg
./zola play clip.mp4 --color=truecolor
```

## Architecture

```
Source (image / video) → Frame { gray, optional RGB }
  → ToneMap(gray) → Mapper → Cell grid { glyph, RGB }
  → Presenter (mono | truecolor SGR)
```

See `AGENTS.md` and `CONTEXT.md` for contributor and domain guidance.

## License

Project-local; add a license when you publish.
