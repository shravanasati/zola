# AGENTS.md — Zola

## What this is

Zola is a high-performance **ASCII media engine** in modern C++23.
It renders images and plays videos as ASCII art in the terminal.

## Status

**v1 MVP is complete** (black-and-white only):

- [x] Still images via stb_image → grayscale Frame → ASCII
- [x] Silent video via FFmpeg → grayscale Frame → timed ASCII playback
- [x] Multi-file layout under `include/zola/` + `src/`
- [x] Makefile (`make`, `make debug`, `make clean`)
- [x] `std::expected` error style; cursor-home presenter; SIGINT restore
- [x] Domain docs: `CONTEXT.md`, this file

**Brightness / ToneMap is landed.** **Color v1 is landed** (truecolor + mono).
ToneMap stays the luminance path for glyph choice under color; do not regress mono.
Optional color follow-ups: ansi256/16, chroma tone, half-blocks (color slice 6).

**Next work: audio** (plan below). Silent video remains the default when muted
or when the container has no audio stream.

## Non-goals (still deferred)

- Interactive TUI chrome, seeking UI, playlists
- Windows / non-POSIX terminals
- GPU / CUDA paths
- Sixel / Kitty / other pixel graphics protocols (ASCII Cell path only)

## Domain language

Read `CONTEXT.md` before renaming types or inventing synonyms.
Prefer: Frame, Cell, Glyph ramp, Source, Mapper, Presenter, Engine.

Visual / tone terms (landed):

- **Tone map** — remap sample luminances (gain, bias, gamma, auto levels)
- **Color sample** — RGB attached to a sample or cell
- **SGR** — ANSI Select Graphic Rendition sequences the Presenter emits
- **Color mode** — Presenter `mono` | `truecolor`

When audio lands, prefer these terms:

- **PCM** — interleaved linear samples ready for the device (not compressed packets)
- **Audio clock** — master timebase for A/V sync (device or Engine wall clock)
- **Audio output** — device backend that consumes PCM (not FFmpeg decode)
- **Mute** — decode path may still run or skip; device must not play

Audio is **not** part of Frame. Do not invent AudioFrame as a Frame subclass.

## Architecture rules

1. **Frame is the visual IR.** Sources produce Frames. Mapper never opens files.
   Presenter never decodes media. **Audio never flows through Frame / Mapper /
   Presenter.**
2. **No new seams without two adapters.** Don't abstract "Logger" or
   "ConfigStore" until something actually varies. Audio: one real Output
   backend for v1 is fine; a mute/null sink counts as the second adapter
   only if it shares the same interface and is used in tests.
3. **Performance is part of the interface.** Hot paths must not allocate
   per frame after startup. Prefer `std::span`, pre-sized `std::vector`,
   and bulk writes. Audio: pre-sized ring buffer; no per-packet heap on
   the steady-state path after open.
4. **Deep modules.** Put complexity inside Mapper / VideoSource / audio
   decode; keep Engine and CLI thin. Engine owns **sync policy** only.
5. **One responsibility per translation unit.** Don't dump everything in
   `main.cpp`.
6. **Brightness before color.** Landed. Tone mapping works on grayscale alone;
   color reuses that path on luminance, then attaches chroma.
7. **Silent-safe audio.** Missing stream, open failure of the device, or
   `--mute` must not break video ASCII playback.

## Build

```bash
make            # release binary: ./zola
make debug      # -O0 -g
make clean
make run-image IMG=path/to.png
make run-video VID=path/to.mp4
```

System deps: FFmpeg development libraries (`libavformat`, `libavcodec`,
`libswscale`, `libswresample`, `libavutil`). stb_image is vendored under
`third_party/`. Audio device backend for v1 is decided in the audio plan
(default: miniaudio header-only under `third_party/`).

## Code style

- C++23, `-Wall -Wextra -Wpedantic`
- **All fallible operations return `std::expected<T, Error>`** (or
  `std::expected<void, Error>`). Do not throw across engine seams.
  CLI maps Error → stderr message + exit code.
- Namespace: `zola`
- Files: `snake_case.cpp` / headers matching purpose
- No `using namespace std`
- Comments only for non-obvious invariants (aspect ratio, timing, buffer ownership)

## Testing expectations

- Pure logic (Mapper, glyph index, tone map) must be unit-testable without a TTY
- File I/O tests optional; prefer small fixture assets
- Manual smoke: `./zola image assets/sample.png` and `./zola play clip.mp4`
- With audio: `./zola play clip.mp4` (sound on) and `./zola play clip.mp4 --mute`

---

## Done: brightness / ToneMap

Landed. Pipeline: `Source → Frame (gray) → ToneMap → Mapper → Presenter`.

| Flag | Default |
|------|---------|
| `--brightness F` | `0` |
| `--contrast F` | `1` |
| `--gamma F` | `1` |
| `--auto-levels` | off |
| `--invert` | off (reverses glyph ramp; not ToneMap) |

Formula: normalize → optional auto-levels → gamma → contrast around mid-gray
+ brightness → clamp → `u8` → ramp. See `tone_map.hpp` / tests.

---

## Done: color v1

Landed. Pipeline:

```
Source → Frame { gray, optional rgb }
      → ToneMap(gray only)
      → AsciiMapper → CellGrid { glyph, r,g,b }
      → Presenter(mono | truecolor) → terminal
```

| Flag | Default |
|------|---------|
| `--color` / `--color=truecolor` | mono |
| `--color=mono` | mono |
| `--bg` | FG only (`38;2`); with `--bg`, FG+BG same RGB |

Locked choices that shipped: extend Frame (not ColorFrame); RGB24 plane; Sources
fill RGB when possible; ToneMap gray-only; box-average RGB into Cells; truecolor
SGR + RLE; invert = glyph ramp only. See `frame.hpp`, `presenter.hpp`,
`tests/color_test.cpp`.

Optional follow-ups (not blocking audio):

- `ansi256` / `ansi16` quantization
- Tone-map influence on RGB / chroma
- Half-block `▀` dual color
- Temporal auto-levels smoothing

---

## Plan: audio (next)

### Goal

Play container audio **in sync** with ASCII video under `./zola play`. Still
images stay silent. Default: audio **on** when a stream exists and the device
opens. `--mute` restores today’s silent behavior. Missing audio must not fail
playback.

### Locked decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Visual IR | **Unchanged** — Frame / Cell stay video-only | Audio is orthogonal; do not overload Frame |
| Where decode lives | **Inside VideoSource (or rename to media demux)** — one `AVFormatContext`, video + audio packets | Avoid double-open demux; deep module owns FFmpeg |
| PCM format | **S16 interleaved stereo** via `libswresample` | Universal device input; simple ring buffer |
| Sample rate | Device rate from open (prefer native / 48 kHz); swr converts | One conversion at decode boundary |
| Output backend | **miniaudio** (header-only in `third_party/`) for v1 | No system audio-dev package; POSIX-friendly; easy mute null |
| Interface | Thin `AudioOutput` { open, start, stop, write/push or callback pull from ring } | Engine never talks Pulse/ALSA/miniaudio details |
| Ring buffer | Fixed-capacity PCM queue, SPSC (decode thread or Engine → device callback) | No unbounded growth; drop-oldest or wait policy documented |
| Sync master | **Audio clock when playing; wall clock when muted / no audio** | Ears are more sensitive than eyes; mute keeps current FPS sleep loop |
| Video late | **Drop / skip sleep debt** (already partially true); never block audio | Prefer short visual hitch over audio underrun |
| Video early | Sleep until PTS (as today) measured against master clock | |
| PTS | Use stream time_base → seconds from media start for both A and V | Shared t0 at play start |
| No audio stream | Soft path: video-only, wall-clock FPS (current) | Silent-safe |
| Device open fail | Log once to stderr; continue silent (do not hard-fail play) | Terminal ASCII still useful over SSH without pulse |
| Volume | v1: full scale; optional `--volume 0..1` if cheap | Can ship mute-only first |
| Image command | No audio path | |
| Threading | Device callback pulls from ring; demux/decode on Engine thread **or** one feeder thread | Prefer **one feeder thread** if Engine sleep would starve audio fill; document choice in commit |
| Ctrl-C | Stop audio device in Presenter/Engine teardown path (before/with terminal restore) | No stuck device or zombie stream |

### Pipeline

```
                    ┌─ video packets → decode → Frame {gray,rgb}
Container (FFmpeg) ─┤                         → ToneMap → Mapper → Presenter
                    └─ audio packets → decode → swr → PCM ring → AudioOutput
                                                              ↑
Engine: master clock (audio pos or wall) ─────────────────────┘
        present video when PTS <= clock; sleep or drop when early/late
```

### Data model (sketch)

```cpp
// Not a Frame. Fixed layout after open.
struct AudioFormat {
  int sample_rate = 0;
  int channels = 2;          // v1: always stereo after swr
  // sample format: S16 interleaved
};

// SPSC ring of int16_t samples (interleaved). Pure; unit-testable.
class PcmRing {
  // try_push / try_pop (or write/read spans); capacity in frames
  // full policy: drop oldest chunk OR block push with timeout — prefer
  // **drop-oldest on overflow** so video demux never hard-deadlocks
};

class AudioOutput {
  VoidResult open(const AudioFormat&);
  void start();
  void stop() noexcept;
  // pull callback registered with miniaudio reads PcmRing
};

// VideoSource gains (names flexible):
//   bool has_audio() const;
//   AudioFormat audio_format() const;
//   // during next_frame / pump: decode available audio into ring
//   VoidResult pump_audio(PcmRing& ring);  // or internal ring owned by source
```

Do **not** put PCM on `Source` virtual API for images. Prefer VideoSource-only
methods, or a narrow optional interface used only by Engine::play_video.

### CLI

| Flag | Meaning | Default |
|------|---------|---------|
| (none) | Play audio when stream + device OK | on |
| `--mute` | No device output; wall-clock video timing | off |
| `--volume F` | Linear gain 0..1 (optional v1; clamp) | `1` |
| existing | `--fps`, color, tone flags unchanged | — |

Reject `--volume` out of range with exit 2 + usage (if flag ships).

### Implementation slices (do in order)

Each slice leaves `make` + `make test` green; silent/`--mute` path must not
regress ASCII timing or Ctrl-C restore.

#### Slice 1 — PCM ring + AudioOutput skeleton

**Files:** `include/zola/pcm_ring.hpp` (header-only ok), `include/zola/audio_output.hpp`,
`src/audio_output.cpp`, `tests/pcm_ring_test.cpp`, Makefile

- Lock SPSC ring API and overflow policy (drop-oldest).
- `AudioOutput` with miniaudio (or stub that compiles without device in CI).
- Unit-test ring: wrap, partial fills, overflow drops.

**Accept:** tests pass; no change to play UX yet.

#### Slice 2 — FFmpeg audio decode into PCM

**Files:** `video_source.hpp/.cpp` (or split `audio_decode` TU if file grows)

- On `open`: `av_find_best_stream` audio; if none, `has_audio()==false`.
- Alloc codec + `SwrContext` → S16 stereo @ chosen rate.
- Packet demux: today video-only loop must become **shared demux** — video
  packets → video decoder; audio packets → audio decoder → ring (or staging).
- Preallocate decode/swr buffers; no per-packet `malloc` after open on hot path.
- Link `libswresample` in Makefile / pkg-config.

**Accept:** optional debug build can drain audio to `/dev/null` or count samples;
`next_frame` still yields video; containers without audio unchanged.

#### Slice 3 — Engine clock + wire AudioOutput

**Files:** `engine.hpp/.cpp`

- `EngineOptions`: `bool mute = false`, optional `float volume = 1.f`.
- `play_video`: if audio and !mute → open/start `AudioOutput`; master clock =
  samples played / rate (or miniaudio time); else wall clock as today.
- Pump audio often enough to avoid underrun (each loop iteration and/or feeder).
- Teardown: stop output even on error / SIGINT (RAII guard like PresenterGuard).
- Do not touch `show_image`.

**Accept:** `./zola play assets/sample.mp4` has sound when file has audio;
`--mute` silent; no-audio file still plays video.

#### Slice 4 — CLI + docs

**Files:** `main.cpp`, `CONTEXT.md`, `README.md`, this file status

- Parse `--mute`, optional `--volume`.
- Document domain terms: PCM, Audio clock, Audio output, Mute.
- Note system: speakers/PipeWire/Pulse via miniaudio; SSH without audio → silent.

**Accept:** full acceptance list below.

#### Slice 5 — Optional follow-ups (not blocking “audio done”)

- `--volume` if deferred from v1
- Better underrun metrics / soft resync (nudge video PTS offset)
- Gapless or external audio file beside video
- Pause/seek (still non-goal for interactive TUI)
- Alternate Output backends (Pulse direct) only when second real backend exists

### Files checklist

| File | Slice | Change |
|------|-------|--------|
| `include/zola/pcm_ring.hpp` | 1 | SPSC PCM ring |
| `include/zola/audio_output.hpp` + `src/audio_output.cpp` | 1, 3 | Device backend (miniaudio) |
| `third_party/miniaudio.h` (or equiv.) | 1 | Vendored backend |
| `include/zola/video_source.hpp` + `src/video_source.cpp` | 2 | Shared demux; audio decode + swr |
| `include/zola/engine.hpp` + `src/engine.cpp` | 3 | Mute/volume; clock; pump; teardown |
| `src/main.cpp` | 4 | `--mute`, optional `--volume` |
| `Makefile` | 1–2 | `libswresample`, new TUs/tests |
| `tests/pcm_ring_test.cpp` | 1 | Ring unit tests |
| `CONTEXT.md` / `README.md` | 4 | Domain + usage |

### Performance budget

- After open: **no unbounded queue growth**; ring capacity fixed (e.g. ~200–500 ms).
- Video hot path: still no Frame/Cell/Presenter heap growth after first size.
- Audio underrun: prefer brief silence over blocking the present loop for long.
- `--mute` / no-audio: must not pay for swr or device open.

### Testing plan

| Kind | What |
|------|------|
| Unit | PcmRing wrap, multi-channel frame counts, overflow drop-oldest |
| Unit | PTS/time_base helpers if extracted pure |
| Manual | `./zola play clip_with_audio.mp4` — audio + ASCII stay roughly in sync |
| Manual | `./zola play clip_with_audio.mp4 --mute` — silent, same visual |
| Manual | `./zola play video_only.mp4` — no error, silent video |
| Manual | Ctrl-C mid-play — terminal restored, audio stops |
| Regression | `./zola image …` unchanged; color/mute flags orthogonal |

### Acceptance (audio v1 done)

- [ ] `./zola play x.mp4` plays container audio when present and device works
- [ ] ASCII video remains timed; no multi-second A/V drift on short clips
- [ ] `./zola play x.mp4 --mute` is silent (wall-clock / FPS path)
- [ ] No-audio containers and device-open failure still show video
- [ ] `./zola image` never opens audio
- [ ] Ctrl-C restores cursor / alt screen and stops audio
- [ ] `make test` includes PCM ring (and any pure sync helpers)
- [ ] Color + tone flags still work during `play` with audio

### Out of scope for audio v1

- Seeking UI, pause keybindings, playlists
- Windows-specific backends as first-class
- Spatial / multi-channel beyond stereo downmix
- Audio-only `./zola play` for music files (unless free with same path)
- Linking Pulse/ALSA directly in addition to miniaudio
- Karaoke / waveform visualizer in the ASCII grid

---

## How to extend (other)

| Feature | Touch |
|---------|--------|
| Webcam | New Source adapter (visual only) |
| Half-block (▀) | Mapper packs 2 source rows; color = upper/lower FG/BG |
| ansi256 / ansi16 | Presenter quantize RGB → SGR index; unit-test cube mapping |
| Second audio backend | New `AudioOutput` impl; Engine selects by option |
| External audio file | Separate open path feeding same PcmRing |

## Do not

- Link OpenCV unless explicitly requested
- Clear the whole screen every video frame (use cursor home)
- Mix decode logic into Presenter
- Mix audio PCM into Frame / Cell / Mapper
- Commit large sample videos
- Regress mono default, ToneMap, or color when adding audio
- Allocate SGR strings per cell with `std::ostringstream` on the hot path
- Introduce a second parallel Source hierarchy only for color
- Hard-fail `play` solely because audio device is missing
- Let the audio ring grow without bound
