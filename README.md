# Zola

High-performance **ASCII media engine** in modern C++. Render images and play videos as ASCII art in your terminal — mono by default, optional truecolor.

## Requirements

- C++23 compiler (GCC 12+ or Clang 16+)
- FFmpeg development libraries: `libavformat`, `libavcodec`, `libswscale`, `libswresample`, `libavutil`
- Linux / POSIX terminal

### Fedora / RHEL

```bash
sudo dnf install gcc-c++ ffmpeg-free-devel
# or full FFmpeg from RPM Fusion if preferred
```

### Debian / Ubuntu

```bash
sudo apt install g++ libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
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

# Video playback with audio (when the container has audio and a device is available)
./zola play path/to/clip.mp4
./zola play path/to/clip.mp4 --mute        # silent, wall-clock timing
./zola play path/to/clip.mp4 --volume 0.5

# Optional sizing
./zola image photo.png --cols 120 --rows 40
./zola play clip.mp4 --fps 24

# Brightness / tone (affects glyph density via luminance)
./zola image photo.png --brightness 0.2
./zola image photo.png --contrast 1.5 --gamma 0.8
./zola play clip.mp4 --auto-levels
./zola image photo.png --invert   # light terminal backgrounds

# URL streaming (FFmpeg handles HTTP/HTTPS transparently)
./zola play https://example.com/video.mp4

# YouTube / other platforms via yt-dlp (URL resolver is external)
zola play "$(yt-dlp --get-url -f best 'https://youtube.com/watch?v=...')"

# Truecolor (24-bit SGR); default remains pure mono ASCII
./zola image photo.png --color
./zola image photo.png --color=truecolor --bg
./zola play clip.mp4 --color=truecolor
```

## Playback Controls

| Key | Action |
|-----|--------|
| `Space` / `Enter` | Toggle pause / resume |
| `→` | Seek forward 5 s |
| `←` | Seek backward 5 s |
| `q` / `Esc` | Quit |

## Architecture

```
Source (image / video) → Frame { gray, optional RGB }
  → ToneMap(gray) → Mapper → Cell grid { glyph, RGB }
  → Presenter (mono | truecolor SGR)

For video with audio, the container is demuxed once; video packets decode to
Frames while audio packets decode (via libswresample) to S16 PCM and play
through a miniaudio backend. The audio device clock drives A/V sync when
playing; `--mute` or missing audio falls back to wall-clock FPS timing.
```

See `AGENTS.md` and `CONTEXT.md` for contributor and domain guidance.

## License

Project-local; add a license when you publish.
