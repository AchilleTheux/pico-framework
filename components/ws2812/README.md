# ws2812

PIO-driven WS2812 / WS2812B / SK6812 addressable LED strips.

## What it does

* Drives one or more strips, each on its own PIO state machine.
* Strips on the same PIO block share a single copy of the loaded program, so
  four strips cost four state machines but only four instructions of program
  memory.
* RGB (24-bit) and RGBW (32-bit) strips.
* Non-destructive global brightness, applied when the frame is sent.
* Colour helpers: named constants, integer HSV, and linear interpolation.

## What it does not do

Transmission is synchronous. `ws2812_show()` blocks for about 30 us per RGB
pixel plus the 300 us latch gap — roughly 1.9 ms for 60 pixels. That matches
DESIGN_DOC.md section 8: the first API is synchronous, and a DMA-backed
asynchronous path is worth adding when an application's main loop actually
cannot afford the wait.

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
