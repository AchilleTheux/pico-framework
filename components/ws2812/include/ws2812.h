/*
 * ws2812 - PIO-driven WS2812 / WS2812B / SK6812 LED strips.
 *
 * The driver holds no pin or strip constants of its own: a caller fills in a
 * ws2812_config_t and owns the pixel buffer (DESIGN_DOC.md section 13). Several
 * strips may coexist, each on its own state machine; strips sharing a PIO block
 * share a single copy of the loaded program.
 *
 * Transmission is synchronous: ws2812_show() returns once the last bit has left
 * the state machine and the strip's latch time has elapsed (DESIGN_DOC.md
 * section 8). Sending 60 RGB pixels takes roughly 1.9 ms.
 */

#ifndef PICO_FRAMEWORK_WS2812_H
#define PICO_FRAMEWORK_WS2812_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

#include "ws2812_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard WS2812B bit rate. Some clones want 400 kHz. */
#define WS2812_DEFAULT_FREQUENCY_HZ 800000u

/* Minimum idle time on the line for a strip to latch the frame it just got. */
#define WS2812_RESET_US 300u

typedef enum {
    WS2812_OK = 0,
    WS2812_ERR_INVALID_ARG,   /* null pointer, zero length, or bad pin */
    WS2812_ERR_NO_STATE_MACHINE,
    WS2812_ERR_NO_PROGRAM_SPACE,
} ws2812_result_t;

typedef struct {
    /* PIO block to drive the strip from. On RP2350 boards with GPIOs above 31
       the caller must have set this block's GPIO base to cover `pin`. */
    PIO pio;

    /* Data pin. Driven by the state machine's side-set. */
    uint pin;

    /* Caller-owned pixel buffer, at least `length` entries, valid for as long
       as the strip is initialised. The driver never allocates. */
    ws2812_color_t *pixels;
    uint16_t length;

    /* SK6812-style strips with a dedicated white LED clock 32 bits per pixel. */
    bool is_rgbw;

    /* 0 selects WS2812_DEFAULT_FREQUENCY_HZ. */
    uint32_t frequency_hz;
} ws2812_config_t;

/* Opaque to callers in practice; laid out here so it can live in .bss. */
typedef struct {
    PIO pio;
    uint sm;
    uint offset;
    uint pin;
    ws2812_color_t *pixels;
    uint16_t length;
    bool is_rgbw;
    uint8_t brightness;
    bool initialised;
} ws2812_strip_t;

/*
 * Claim a state machine, load the program if this PIO block does not already
 * have it, and configure the pin. The strip is left dark: the pixel buffer is
 * cleared but nothing is transmitted until ws2812_show().
 */
ws2812_result_t ws2812_init(ws2812_strip_t *strip, const ws2812_config_t *config);

/* Release the state machine, and the program once the last strip on that PIO
   block has gone. Does not change the pin's state. */
void ws2812_deinit(ws2812_strip_t *strip);

/* Writes past the end of the strip are ignored rather than corrupting memory. */
void ws2812_set_pixel(ws2812_strip_t *strip, uint16_t index, ws2812_color_t color);

/* Out-of-range reads return black. */
ws2812_color_t ws2812_get_pixel(const ws2812_strip_t *strip, uint16_t index);

void ws2812_fill(ws2812_strip_t *strip, ws2812_color_t color);
void ws2812_clear(ws2812_strip_t *strip);

/*
 * Global scale applied when the frame is sent, so the buffer keeps the colours
 * the application authored. Defaults to 255 (unchanged).
 */
void ws2812_set_brightness(ws2812_strip_t *strip, uint8_t brightness);
uint8_t ws2812_get_brightness(const ws2812_strip_t *strip);

static inline uint16_t ws2812_length(const ws2812_strip_t *strip)
{
    return strip->length;
}

/*
 * Transmit the buffer and wait out the latch time. Blocks for roughly
 * 30 us per RGB pixel plus WS2812_RESET_US.
 */
void ws2812_show(ws2812_strip_t *strip);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WS2812_H */
