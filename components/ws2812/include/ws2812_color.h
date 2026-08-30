/*
 * ws2812_color - colour representation and wire encoding for WS2812 strips.
 *
 * This header is deliberately free of Pico SDK dependencies: everything here
 * is integer arithmetic, so it builds and is unit-tested on the host
 * (DESIGN_DOC.md section 19). The hardware side lives in ws2812.h.
 */

#ifndef PICO_FRAMEWORK_WS2812_COLOR_H
#define PICO_FRAMEWORK_WS2812_COLOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A colour in human order, independent of what a given strip puts on the wire.
 * `w` is only meaningful for RGBW strips and is ignored otherwise.
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
} ws2812_color_t;

#define WS2812_COLOR_BLACK   ((ws2812_color_t){   0,   0,   0, 0 })
#define WS2812_COLOR_WHITE   ((ws2812_color_t){ 255, 255, 255, 0 })
#define WS2812_COLOR_RED     ((ws2812_color_t){ 255,   0,   0, 0 })
#define WS2812_COLOR_GREEN   ((ws2812_color_t){   0, 255,   0, 0 })
#define WS2812_COLOR_BLUE    ((ws2812_color_t){   0,   0, 255, 0 })
#define WS2812_COLOR_YELLOW  ((ws2812_color_t){ 255, 255,   0, 0 })
#define WS2812_COLOR_CYAN    ((ws2812_color_t){   0, 255, 255, 0 })
#define WS2812_COLOR_MAGENTA ((ws2812_color_t){ 255,   0, 255, 0 })

static inline ws2812_color_t ws2812_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (ws2812_color_t){ .r = r, .g = g, .b = b, .w = 0 };
}

static inline ws2812_color_t ws2812_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    return (ws2812_color_t){ .r = r, .g = g, .b = b, .w = w };
}

/*
 * Pack a colour into the 32-bit word the PIO program expects.
 *
 * The state machine shifts left (MSB first) with an autopull threshold of 24
 * bits for RGB and 32 for RGBW, so the bits it transmits are the *top* ones.
 * The result is therefore left-aligned: GRB in bits 31..8 for RGB, and GRBW in
 * bits 31..0 for RGBW. For RGB strips the low byte is zero and never clocked
 * out.
 */
static inline uint32_t ws2812_color_to_wire(ws2812_color_t c, bool is_rgbw)
{
    uint32_t word = ((uint32_t)c.g << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.b << 8);
    return is_rgbw ? (word | (uint32_t)c.w) : word;
}

/*
 * Scale every channel by `brightness`, where 255 leaves the colour unchanged
 * and 0 produces black. Rounding is to nearest, so a channel only reaches 0
 * when it genuinely should.
 */
ws2812_color_t ws2812_color_scale(ws2812_color_t c, uint8_t brightness);

/*
 * Linear interpolation between `from` (t = 0) and `to` (t = 255), per channel.
 */
ws2812_color_t ws2812_color_lerp(ws2812_color_t from, ws2812_color_t to, uint8_t t);

/*
 * Integer HSV to RGB, with all three inputs on 0..255 and hue wrapping at 256.
 * Useful for rainbows and status fades without pulling in floating point.
 * The white channel of the result is always 0.
 */
ws2812_color_t ws2812_color_from_hsv(uint8_t h, uint8_t s, uint8_t v);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WS2812_COLOR_H */
