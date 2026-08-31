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

## Wire format

The PIO program shifts left with an autopull threshold of 24 bits (32 for
RGBW), so the bits it clocks out are the *high* ones and the channel order on
the wire is green, red, blue:

```text
bit 31                                                              bit 0
+-----------+-----------+-----------+-----------+
|   green   |    red    |   blue    |   white   |   <- white only when is_rgbw
+-----------+-----------+-----------+-----------+
```

`ws2812_color_to_wire()` in `ws2812_color.h` is the single place this layout is
expressed, and `tests/components/ws2812_color_test.c` pins it down. If a strip
shows red where green was asked for, it is a clone with a different channel
order, not a bug in the encoding.

## Layout

| File | Role |
|------|------|
| `ws2812.pio` | the bit-banging program, unmodified from pico-examples (BSD-3-Clause) |
| `ws2812.c` | state machine setup and transmission — all the Pico SDK calls |
| `ws2812_color.c` / `include/ws2812_color.h` | pure integer colour logic, no SDK dependency |
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

* Host: `make test` covers the colour and wire-encoding logic.
* Hardware: `make APP=tests/ws2812_test flash` — see that test's README.

The blocking PIO path and RGB colour order were visually confirmed on the
Waveshare RP2040-Zero's onboard GPIO16 pixel on 2026-08-31. External strips,
RGBW pixels, long-strip signal integrity, and the DMA path remain unverified on
hardware.
