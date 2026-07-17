# Zola

A terminal media engine that maps visual frames to ASCII cells and presents them.

## Language

**Zola**:
The ASCII media engine product and its top-level orchestration.
_Avoid_: player, viewer (too narrow)

**Frame**:
A rectangular grid of grayscale samples (0–255 luminance per sample), with an optional interleaved RGB24 plane (same WxH). The engine's universal intermediate representation.
_Avoid_: Image, bitmap, buffer

**Cell**:
One terminal character position produced from one or more samples (glyph plus optional RGB color sample).
_Avoid_: Pixel, character

**Cell grid**:
A rectangular array of Cells ready for presentation.
_Avoid_: Screen, canvas

**Glyph ramp**:
An ordered string of ASCII characters from darkest to lightest density used to map luminance to a glyph.
_Avoid_: charset, palette

**Source**:
Anything that produces a sequence of Frames (a still image is one frame; a video is many).
_Avoid_: Loader, decoder

**Mapper**:
Maps a Frame to a Cell grid using a Glyph ramp.
_Avoid_: Converter, renderer

**Presenter**:
Writes a Cell grid to the terminal without scrolling (cursor home and overwrite).
_Avoid_: Output, printer, display

**Engine**:
Orchestrates Source → ToneMap → Mapper → Presenter, including timing for video playback.
_Avoid_: App, runtime

**Tone map**:
Remaps Frame luminances (gain, bias, gamma, auto-levels) before the Mapper chooses glyphs. Pure: no I/O, no ANSI. Sources stay decode-to-gray; brightness lives only here.
_Avoid_: filter, color grade, post-process

**Color sample**:
RGB attached to a Frame sample plane or Cell. Glyph choice still comes from tone-mapped luminance; chroma is box-averaged from the Frame RGB plane.
_Avoid_: pixel color (use with Frame/Cell)

**SGR**:
ANSI Select Graphic Rendition sequences the Presenter emits for color (truecolor `38;2` / optional `48;2`).
_Avoid_: ANSI color codes (prefer SGR when discussing Presenter output)

**Color mode**:
Presenter setting: `mono` (glyphs only) or `truecolor` (24-bit SGR). Default is mono.
_Avoid_: color depth, palette mode (until ansi256/16 land)
