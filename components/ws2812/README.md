# ws2812

PIO-driven WS2812 / WS2812B / SK6812 addressable LED strips.

## What it does

* Drives one or more strips, each on its own PIO state machine.
* Strips on the same PIO block share a single copy of the loaded program, so
  four strips cost four state machines but only four instructions of program
  memory.
* RGB (24-bit) and RGBW (32-bit) strips.
* Non-destructive global brightness, applied when the frame is sent.
* Optional DMA transmission, so a main loop need not wait 1.9 ms a frame.
* Colour helpers: named constants, integer HSV, and linear interpolation.

## Blocking or DMA

The wire takes about 30 us per RGB pixel plus a 300 us latch gap either way —
roughly 1.9 ms for 60 pixels. What differs is whether the processor spends it
waiting.

| | Needs | `show()` | `show_async()` |
|---|---|---|---|
| FIFO | nothing extra | blocks ~1.9 ms | not available |
| DMA | a `wire_buffer` | blocks ~1.9 ms | returns in microseconds |

```c
static ws2812_color_t pixels[60];
static uint32_t wire_buffer[60];        /* what enables DMA */

const ws2812_config_t config = {
    .pio = pio0, .pin = 10,
    .pixels = pixels, .length = count_of(pixels),
    .wire_buffer = wire_buffer,
};
```

Then a main loop never has to wait:

```c
if (!ws2812_is_busy(&strip)) {
    paint(&strip);
    ws2812_show_async(&strip);          /* returns at once */
}
/* ... the rest of the control cycle ... */
```

`show_async()` returns `WS2812_ERR_BUSY` if the previous frame is still going
out; an animation should treat that as "skip this frame" rather than an error.

**The wire buffer has to be separate from the pixels.** The wire format is a
different packing of the same colours and brightness is applied on the way out,
so the array handed to DMA cannot be the one the application authors into. It
costs four bytes a pixel — 240 bytes for a 60-LED strip — and buys the property
that matters: the pixel buffer is copied during `show_async()`, not read during
the transfer, so it can be modified again the moment the call returns.

Leaving `wire_buffer` NULL keeps the FIFO path and costs nothing, which is all
a caller that sets a colour occasionally needs.

`is_busy()` accounts for all three phases: DMA still feeding the FIFO, the
state machine still clocking out the last pixel, and the latch gap after it.
The gap is timed from when the machine went quiet rather than from the start of
the frame, which is why `is_busy()` takes a non-const strip — it records the
deadline the first time it sees the transfer end.

Both paths share one implementation of the transfer, so the blocking and
non-blocking cases cannot drift apart.

## Usage

```c
#include "ws2812.h"

/* The application owns the buffer; the component never allocates. */
static ws2812_color_t pixels[16];
static ws2812_strip_t strip;

const ws2812_config_t config = {
    .pio     = pio0,
    .pin     = 10,          /* from the board header, not hard-coded here */
    .pixels  = pixels,
    .length  = count_of(pixels),
    .is_rgbw = false,
};

if (ws2812_init(&strip, &config) != WS2812_OK) {
    /* out of state machines or program space */
}

ws2812_set_brightness(&strip, 64);
ws2812_fill(&strip, WS2812_COLOR_BLUE);
ws2812_set_pixel(&strip, 0, ws2812_color_from_hsv(hue, 255, 255));
ws2812_show(&strip);
```

Link it from the application:

```cmake
target_link_libraries(app_my_firmware PRIVATE pico_framework::ws2812)
```

## Wire format, and channel order

The PIO program shifts left with an autopull threshold of 24 bits (32 for
RGBW), so the bits it clocks out are the *high* ones:

```text
bit 31                                                              bit 0
+-----------+-----------+-----------+-----------+
|   first   |  second   |   third   |   white   |   <- white only when is_rgbw
+-----------+-----------+-----------+-----------+
```

Which colour goes first is a property of the strip, not of the protocol.
WS2812 and WS2812B want green first, and so do most SK6812 — but WS2815 and
the various clones are found in every permutation, with nothing on the reel to
say which. So `ws2812_config_t` carries an `order`:

```c
const ws2812_config_t config = {
    .pio = pio0, .pin = 6, .pixels = pixels, .length = 300,
    .order = WS2812_ORDER_RGB,      /* zero is WS2812_ORDER_GRB */
};
```

**Two channels swapped is always an order mismatch, never a broken encoder.**
Ask for red and get green, ask for green and get red, with blue correct, and
the strip is RGB where the default assumes GRB — blue sits third in both,
which is exactly why only two of the three ever look wrong. All six
permutations are available: `GRB RGB BRG RBG GBR BGR`.

Rather than a rebuild per guess, `ws2812_set_order()` changes it on a running
strip, so an unlabelled reel can be identified by setting a red colour and
trying orders until it looks red. `apps/home_led` puts that behind an `order`
console command. Put the answer in a profile once it is known.

`ws2812_color_to_wire()` in `ws2812_color.h` is the single place the layout is
expressed, and `tests/components/ws2812_color_test.c` pins down every
permutation, including that the red/green swap above is exactly GRB against
RGB.

## Making it look right: gamma, dithering, and hue

Three things in `ws2812_color.h` exist because a strip driven with plain
linear values does not look the way the numbers say it should.

**Gamma.** A WS2812 emits in proportion to the value it is sent; the eye does
not see in proportion to emitted light. Uncorrected, a linear fade spends most
of its travel already looking bright, and the bottom of a dimming curve
collapses into a few indistinguishable steps. `ws2812_gamma_table` is a
compile-time gamma-2.2 table — 256 bytes of flash, no RAM, no floating point.

Hand it to the driver rather than applying it yourself:

```c
ws2812_set_gamma(&strip, ws2812_gamma_table);
```

**Order matters, and it is the reason that is a driver call.** Gamma says what
value produces a given apparent brightness, so scaling a corrected value undoes
the correction. An application that gamma-corrects its own pixel buffer and
then calls `ws2812_set_brightness()` gets neither: it gets a curve applied to a
curve. `ws2812_set_gamma()` puts the table at the end of the pipeline, after
brightness, where the value is about to go out on the wire. Both the DMA and
the FIFO path go through one encode function so the two cannot end up
disagreeing about it.

**Dithering.** Scaling to a low brightness quantises hard: at brightness 8
every input from 0 to 31 lands on the same output, so a gradient becomes a
staircase. `ws2812_dither_bias()` gives a 2x2 Bayer threshold over the pixel's
position, complemented on alternate frames, and
`ws2812_color_scale_dither()` applies it — recovering roughly two extra bits of
depth that the eye integrates back together.

```c
for (uint16_t i = 0; i < ws2812_length(&strip); i++) {
    const uint8_t bias = ws2812_dither_bias(i, 0, frame);
    ws2812_set_pixel(&strip, i, ws2812_color_scale_dither(colour, brightness, bias));
}
frame++;
```

The complement on odd frames is the part worth getting right. Permuting *which*
pixel gets which threshold — indexing the Bayer matrix with `index ^ 3`, which
is the form this is usually written in — leaves the set of thresholds in the
frame unchanged, so the grain moves around instead of cancelling. Complementing
the value makes the two frames average to the middle of the range exactly, and
`dither_averages_back_to_the_undithered_scale` in the host tests is what holds
that.

**Hue.** `ws2812_color_from_hsv()` has 256 hue steps, which is coarse once a
gradient is spread along a long strip. `ws2812_color_from_hue16()` walks six
256-wide segments instead — `WS2812_HUE16_RANGE` is 1536 — and wraps on its
own, so an accumulator can just keep running.

## Layout

| File | Role |
|------|------|
| `ws2812.pio` | the bit-banging program, unmodified from pico-examples (BSD-3-Clause) |
| `ws2812.c` | state machine setup and transmission — all the Pico SDK calls |
| `ws2812_color.c` / `include/ws2812_color.h` | pure integer colour logic — scaling, lerp, HSV, gamma, dithering — no SDK dependency |
| `include/ws2812.h` | the hardware-facing API |

The split is what makes the colour logic host-testable (DESIGN_DOC.md section
19). Keep new pure logic on the `ws2812_color` side.

## Caveats

* **GPIO base on RP2350B.** For data pins above GPIO 31, set the PIO block's
  GPIO base before `ws2812_init()`; the component does not change it, because
  the setting is shared by every state machine on that block.
* **Level shifting.** A 3.3 V data line is below the WS2812B's specified logic
  high at 5 V. It usually works, and is usually the first thing to suspect when
  a long strip shows corruption after the first few LEDs.
* **Power.** Anything past a handful of LEDs at full white needs its own supply,
  with grounds tied together.

## Testing

* Host: `make test` covers the colour and wire-encoding logic, including the
  gamma table's shape and endpoints, hue-wheel continuity across every segment
  boundary and the wrap, and that dithering averages back to the undithered
  scale rather than shifting the colour.
* Hardware: `make APP=tests/ws2812_test flash` — see that test's README.

The blocking PIO path and GRB colour order were visually confirmed on the
Waveshare RP2040-Zero's onboard GPIO16 pixel on 2026-08-31.

The DMA path and `WS2812_ORDER_RGB` were confirmed on 2026-09-03 on a
300-pixel **WS2815** strip at 12 V behind a 5 V level shifter, driven by
`apps/home_led` from a Pico 2 W: with the GRB default the primaries came out
swapped exactly as described above, and selecting RGB made red, green and blue
correct. Worth recording that this program's timing suits WS2815 at least as
well as the part it is named for — `T0H` 250 ns and `T1H` 875 ns sit
comfortably inside WS2815's 150–450 ns and 750–1050 ns windows, where the
former is right on WS2812B's lower edge. WS2815 also wants a longer latch
gap, ≥280 µs against WS2812B's 50 µs; `WS2812_RESET_US` is 300, so it is
covered, but that is the constant to raise if the far end of a strip ever lags
a frame. All 300 pixels lit, so signal integrity holds over that length behind
a level shifter.

Gamma and dithering were finally judged by the only instrument that can settle
them, on 2026-09-04 and on the same strip: a ramp from `bri 10` to `bri 250`
is smooth, with no visible step backwards, and the low end still shows
gradations rather than a staircase. RGBW pixels remain unverified on hardware.
