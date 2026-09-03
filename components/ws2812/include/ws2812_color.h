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
#include <stddef.h>
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
 * The order a strip expects its three colour bytes in.
 *
 * Not a detail anyone gets to ignore. WS2812 and WS2812B send green first,
 * and so do most SK6812 -- but WS2815 strips and the various clones are found
 * in every permutation, with nothing on the reel to say which. The symptom is
 * unmistakable once you know it: ask for red and get green, ask for green and
 * get red, while blue is fine, and the strip is RGB rather than GRB. Two
 * channels swapped is always an order mismatch, never a broken encoder.
 *
 * The names read in wire order: WS2812_ORDER_GRB puts green on the wire
 * first. On an RGBW strip the white byte is always fourth, whichever of these
 * is chosen.
 */
typedef enum {
    WS2812_ORDER_GRB = 0,   /* WS2812, WS2812B, most SK6812 -- the default */
    WS2812_ORDER_RGB,       /* many WS2815 strips, and clones */
    WS2812_ORDER_BRG,
    WS2812_ORDER_RBG,
    WS2812_ORDER_GBR,
    WS2812_ORDER_BGR,
} ws2812_order_t;

/* "GRB", "RGB", ... for a console that lets someone try them. */
const char *ws2812_order_name(ws2812_order_t order);

/* Parse one of those names, case-insensitively. False if it is not one. */
bool ws2812_order_from_name(const char *name, ws2812_order_t *out);

/*
 * Pack a colour into the 32-bit word the PIO program expects.
 *
 * The state machine shifts left (MSB first) with an autopull threshold of 24
 * bits for RGB and 32 for RGBW, so the bits it transmits are the *top* ones.
 * The result is therefore left-aligned: the three colour bytes in `order`
 * occupy bits 31..8, and white bits 7..0 for RGBW. For RGB strips the low
 * byte is zero and never clocked out.
 *
 * The order is an argument rather than a constant because getting it wrong is
 * a normal thing to do with an unlabelled strip, and a silent default is what
 * makes it hard to find.
 */
static inline uint32_t ws2812_color_to_wire(ws2812_color_t c, bool is_rgbw,
                                            ws2812_order_t order)
{
    uint8_t first;
    uint8_t second;
    uint8_t third;

    switch (order) {
        case WS2812_ORDER_RGB: first = c.r; second = c.g; third = c.b; break;
        case WS2812_ORDER_BRG: first = c.b; second = c.r; third = c.g; break;
        case WS2812_ORDER_RBG: first = c.r; second = c.b; third = c.g; break;
        case WS2812_ORDER_GBR: first = c.g; second = c.b; third = c.r; break;
        case WS2812_ORDER_BGR: first = c.b; second = c.g; third = c.r; break;
        case WS2812_ORDER_GRB:
        default:               first = c.g; second = c.r; third = c.b; break;
    }

    const uint32_t word = ((uint32_t)first << 24) | ((uint32_t)second << 16) |
                          ((uint32_t)third << 8);

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

/*
 * A hue-only wheel at six times the resolution of ws2812_color_from_hsv().
 *
 * 256 hue steps is coarse for a gradient spread along a long strip: a 300-LED
 * rainbow at one hue step per LED runs through the whole wheel more than
 * once, and at a fraction of a step per LED the banding is visible. This
 * walks six 256-wide segments instead -- red, yellow, green, cyan, blue,
 * magenta and back -- so a strip of any practical length gets a smooth ramp.
 *
 * `hue` wraps at WS2812_HUE16_RANGE, so a caller can let an accumulator run
 * and never has to take a modulus of its own. Saturation and value are always
 * full; scale the result with ws2812_color_scale() for anything dimmer.
 */
#define WS2812_HUE16_RANGE 1536u

ws2812_color_t ws2812_color_from_hue16(uint16_t hue);

/* ---------------------------------------------------------------------------
 * Gamma
 * -------------------------------------------------------------------------*/

/*
 * Perceptual correction, gamma 2.2.
 *
 * A WS2812 emits in proportion to the value it is sent, and the eye does not
 * see in proportion to emitted light: half the value is nowhere near half as
 * bright to look at. Uncorrected, a linear fade spends most of its travel
 * looking already-bright, and the bottom of a dimming curve collapses into a
 * few indistinguishable steps.
 *
 * The table is a compile-time constant rather than something built at startup
 * with powf(): it costs 256 bytes of flash, no RAM, and no floating point on
 * a part that has none in hardware.
 *
 * ORDER MATTERS. Gamma belongs at the very end, after brightness scaling --
 * correcting first and then scaling re-linearises what you just corrected.
 * ws2812_set_gamma() applies it in the right place for you; reach for these
 * two directly only when authoring pixels the driver will not be scaling.
 */
uint8_t ws2812_gamma8(uint8_t value);

/* Per-channel ws2812_gamma8(), white included. */
ws2812_color_t ws2812_color_gamma(ws2812_color_t c);

/* The table itself, for handing to ws2812_set_gamma(). */
extern const uint8_t ws2812_gamma_table[256];

/* ---------------------------------------------------------------------------
 * Dithering
 * -------------------------------------------------------------------------*/

/*
 * Ordered dithering, for the bottom of the brightness range.
 *
 * Scaling to a low brightness quantises hard: at brightness 8 every input
 * from 0 to 31 lands on the same output, so a gradient becomes a staircase
 * and a slow fade becomes visible steps. Varying the rounding threshold per
 * pixel and per frame trades that for a fine grain the eye integrates away,
 * recovering roughly two extra bits of depth without touching the wire format.
 *
 * ws2812_dither_bias() is a 2x2 Bayer matrix over the pixel's position,
 * inverted on alternate frames so the pattern does not sit still and read as
 * texture. Pass `y` as the strip index when several strips are driven
 * together, or 0 for one strip.
 */
#define WS2812_DITHER_LEVELS 4u

uint8_t ws2812_dither_bias(uint16_t x, uint16_t y, uint32_t frame);

/*
 * ws2812_color_scale() with the rounding threshold displaced by `bias`
 * (0..WS2812_DITHER_LEVELS-1, from ws2812_dither_bias()).
 *
 * Averaged over the four bias values the result matches ws2812_color_scale()
 * to within one step, which is what makes the grain cancel rather than shift
 * the colour.
 */
ws2812_color_t ws2812_color_scale_dither(ws2812_color_t c, uint8_t brightness,
                                         uint8_t bias);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WS2812_COLOR_H */
