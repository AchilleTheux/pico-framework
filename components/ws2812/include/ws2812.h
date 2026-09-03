/*
 * ws2812 - PIO-driven WS2812 / WS2812B / SK6812 LED strips.
 *
 * The driver holds no pin or strip constants of its own: a caller fills in a
 * ws2812_config_t and owns the pixel buffer (DESIGN_DOC.md section 13). Several
 * strips may coexist, each on its own state machine; strips sharing a PIO block
 * share a single copy of the loaded program.
 *
 * Transmission can be either blocking or DMA-driven. Sending 60 RGB pixels
 * occupies the wire for about 1.9 ms either way; the difference is whether the
 * processor spends it waiting. Give the strip a `wire_buffer` and
 * ws2812_show_async() hands the frame to DMA and returns immediately, which is
 * what a main loop with a control cycle to run actually wants.
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
    WS2812_ERR_NO_DMA_CHANNEL,
    WS2812_ERR_NO_WIRE_BUFFER, /* async asked for without one configured */
    WS2812_ERR_BUSY,           /* the previous frame is still going out */
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

    /* Which order this strip wants its colour bytes in. Zero is
       WS2812_ORDER_GRB, which is what WS2812/WS2812B want, so a caller that
       does not know to care gets the common case. */
    ws2812_order_t order;

    /* 0 selects WS2812_DEFAULT_FREQUENCY_HZ. */
    uint32_t frequency_hz;

    /*
     * Optional, and what enables DMA. Caller-owned, at least `length` words,
     * valid for as long as the strip is initialised.
     *
     * It has to be separate from `pixels` rather than shared with it: the wire
     * format is a different packing of the same colours, and brightness is
     * applied on the way out, so the buffer handed to DMA cannot be the one the
     * application authors into. That costs four bytes a pixel — 240 bytes for a
     * 60-LED strip — in exchange for not blocking the processor for 1.9 ms a
     * frame.
     *
     * NULL leaves only the blocking path, which needs no extra memory.
     */
    uint32_t *wire_buffer;
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
    ws2812_order_t order;
    uint8_t brightness;
    const uint8_t *gamma_table;   /* NULL when correction is off */

    uint32_t *wire_buffer;
    int dma_channel;        /* -1 when there is no wire buffer */

    /* When the strip may next be written to, in microseconds. Set when the
       state machine runs dry, so the latch gap is enforced without blocking. */
    uint64_t latch_ready_us;

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

/*
 * Optional perceptual correction, applied per channel as the frame is encoded
 * -- after brightness, which is the only order that works.
 *
 * Gamma maps a value to what it has to be for the eye to read it as that
 * fraction of full. Scaling a corrected value undoes the correction, so an
 * application that gamma-corrects its own pixel buffer and then calls
 * ws2812_set_brightness() gets neither: it gets a curve applied to a curve.
 * Handing the table here instead puts the correction at the end of the
 * pipeline, where the value is otherwise about to go out on the wire.
 *
 * `table` is borrowed, not copied, and must outlive the strip --
 * ws2812_gamma_table is a constant and always satisfies that. NULL turns
 * correction off, which is the default and what a strip used for signalling
 * rather than lighting wants.
 *
 * The white channel of an RGBW strip is corrected along with the others.
 */
void ws2812_set_gamma(ws2812_strip_t *strip, const uint8_t *table);

/*
 * Change the wire order of a running strip.
 *
 * Here because identifying an unlabelled strip is a matter of trying orders
 * until red is red, and doing that from a console beats doing it from a
 * rebuild. Takes effect on the next frame.
 */
void ws2812_set_order(ws2812_strip_t *strip, ws2812_order_t order);
ws2812_order_t ws2812_get_order(const ws2812_strip_t *strip);

static inline uint16_t ws2812_length(const ws2812_strip_t *strip)
{
    return strip->length;
}

/*
 * Transmit the buffer and wait until the strip is ready for the next frame.
 * Blocks for roughly 30 us per RGB pixel plus WS2812_RESET_US.
 *
 * Uses DMA when a wire buffer was configured and the FIFO otherwise; either
 * way it returns only when the frame is out and latched.
 */
void ws2812_show(ws2812_strip_t *strip);

/*
 * Hand the frame to DMA and return at once.
 *
 * Requires a wire buffer. The pixel buffer is read during this call, not
 * during the transfer, so it may be modified as soon as this returns — the
 * copy into wire format is what makes that safe.
 *
 * Returns WS2812_ERR_BUSY if the previous frame is still going out or its latch
 * gap has not elapsed; nothing is sent in that case. A caller driving
 * animations should treat that as "skip this frame", not as an error.
 */
ws2812_result_t ws2812_show_async(ws2812_strip_t *strip);

/*
 * True while a frame is still being transmitted, or while the latch gap after
 * one has yet to elapse. False means ws2812_show_async() will accept a frame.
 */
bool ws2812_is_busy(ws2812_strip_t *strip);

/* Block until ws2812_is_busy() is false. Returns immediately if it already is. */
void ws2812_wait(ws2812_strip_t *strip);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WS2812_H */
