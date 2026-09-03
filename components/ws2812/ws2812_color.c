#include "ws2812_color.h"

/* Round-to-nearest scale of one channel: (value * numerator) / 255. */
static uint8_t scale_channel(uint8_t value, uint8_t numerator)
{
    return (uint8_t)(((uint16_t)value * numerator + 127u) / 255u);
}

ws2812_color_t ws2812_color_scale(ws2812_color_t c, uint8_t brightness)
{
    return (ws2812_color_t){
        .r = scale_channel(c.r, brightness),
        .g = scale_channel(c.g, brightness),
        .b = scale_channel(c.b, brightness),
        .w = scale_channel(c.w, brightness),
    };
}

static uint8_t lerp_channel(uint8_t from, uint8_t to, uint8_t t)
{
    /* Signed arithmetic so a descending ramp rounds the same way as an
       ascending one. */
    int32_t delta = (int32_t)to - (int32_t)from;
    return (uint8_t)((int32_t)from + (delta * (int32_t)t + (delta >= 0 ? 127 : -127)) / 255);
}

ws2812_color_t ws2812_color_lerp(ws2812_color_t from, ws2812_color_t to, uint8_t t)
{
    return (ws2812_color_t){
        .r = lerp_channel(from.r, to.r, t),
        .g = lerp_channel(from.g, to.g, t),
        .b = lerp_channel(from.b, to.b, t),
        .w = lerp_channel(from.w, to.w, t),
    };
}

ws2812_color_t ws2812_color_from_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    if (s == 0) {
        return ws2812_rgb(v, v, v);
    }

    /* Six 43-wide sectors do not tile 256 evenly, so the last sector is one
       step short; the visible effect is a hue step of well under one degree. */
    const uint8_t sector = (uint8_t)(h / 43u);
    const uint8_t offset = (uint8_t)((h - sector * 43u) * 6u);

    const uint8_t p = (uint8_t)((v * (255u - s)) / 255u);
    const uint8_t q = (uint8_t)((v * (255u - ((uint16_t)s * offset) / 255u)) / 255u);
    const uint8_t t = (uint8_t)((v * (255u - ((uint16_t)s * (255u - offset)) / 255u)) / 255u);

    switch (sector) {
        case 0:  return ws2812_rgb(v, t, p);
        case 1:  return ws2812_rgb(q, v, p);
        case 2:  return ws2812_rgb(p, v, t);
        case 3:  return ws2812_rgb(p, q, v);
        case 4:  return ws2812_rgb(t, p, v);
        default: return ws2812_rgb(v, p, q);
    }
}

ws2812_color_t ws2812_color_from_hue16(uint16_t hue)
{
    /* Six segments of 256, so the segment is the high bits and the position
       within it the low byte -- no division anywhere on this path. */
    const uint16_t wrapped = (uint16_t)(hue % WS2812_HUE16_RANGE);
    const uint8_t segment = (uint8_t)(wrapped >> 8);
    const uint8_t position = (uint8_t)(wrapped & 0xFFu);
    const uint8_t inverse = (uint8_t)(255u - position);

    switch (segment) {
        case 0:  return ws2812_rgb(255, position, 0);        /* red     -> yellow  */
        case 1:  return ws2812_rgb(inverse, 255, 0);         /* yellow  -> green   */
        case 2:  return ws2812_rgb(0, 255, position);        /* green   -> cyan    */
        case 3:  return ws2812_rgb(0, inverse, 255);         /* cyan    -> blue    */
        case 4:  return ws2812_rgb(position, 0, 255);        /* blue    -> magenta */
        default: return ws2812_rgb(255, 0, inverse);         /* magenta -> red     */
    }
}

/*
 * round(pow(i / 255, 2.2) * 255) for i in 0..255. Monotonic, and fixed at both
 * ends: 0 stays off and 255 stays full, so correction never costs the range.
 */
const uint8_t ws2812_gamma_table[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,
      3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
      6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,
     11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,
     16,  16,  17,  17,  18,  18,  19,  19,  20,  20,  21,  22,
     22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
     30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,
     39,  39,  40,  41,  42,  43,  43,  44,  45,  46,  47,  48,
     49,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
     73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,
     87,  88,  89,  90,  91,  93,  94,  95,  97,  98,  99, 100,
    102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 116, 117,
    119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
    137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154,
    156, 158, 159, 161, 163, 165, 166, 168, 170, 172, 173, 175,
    177, 179, 181, 182, 184, 186, 188, 190, 192, 194, 196, 197,
    199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246,
    248, 251, 253, 255,
};

uint8_t ws2812_gamma8(uint8_t value)
{
    return ws2812_gamma_table[value];
}

ws2812_color_t ws2812_color_gamma(ws2812_color_t c)
{
    return (ws2812_color_t){
        .r = ws2812_gamma_table[c.r],
        .g = ws2812_gamma_table[c.g],
        .b = ws2812_gamma_table[c.b],
        .w = ws2812_gamma_table[c.w],
    };
}

uint8_t ws2812_dither_bias(uint16_t x, uint16_t y, uint32_t frame)
{
    /* The 2x2 Bayer matrix, in the order a 2-bit (y, x) index reads it. */
    static const uint8_t matrix[4] = { 0u, 2u, 3u, 1u };

    const uint8_t index = (uint8_t)((x & 1u) | ((y & 1u) << 1));
    const uint8_t bias = matrix[index];

    /*
     * Odd frames get the complement, which is what stops a still image from
     * showing the pattern as fixed texture: each pixel alternates between a
     * threshold above and one below where it would otherwise sit, and the two
     * average to the middle of the range exactly.
     *
     * Complementing the *value* rather than permuting the index is the part
     * worth being careful about. Indexing the matrix with `index ^ 3` looks
     * like it does the same thing and does not -- it swaps which pixel gets
     * which threshold while leaving the set of thresholds in the frame
     * unchanged, so the grain moves around instead of cancelling.
     */
    return (frame & 1u) ? (uint8_t)(WS2812_DITHER_LEVELS - 1u - bias) : bias;
}

/*
 * Scale one channel, rounding at `offset` instead of at the usual half-step.
 *
 * ws2812_color_scale() adds 127, i.e. rounds to nearest. Sweeping the offset
 * across the quantisation interval instead -- 32, 96, 160, 224 for the four
 * bias values -- makes each pixel round up at a different point, so a value
 * that sits between two outputs lands on the lower one in some pixels and the
 * higher one in others. Those four offsets average 128, which is why the
 * dithered result averages back to the undithered one.
 */
static uint8_t scale_channel_biased(uint8_t value, uint8_t numerator, uint16_t offset)
{
    return (uint8_t)(((uint16_t)value * numerator + offset) / 255u);
}

ws2812_color_t ws2812_color_scale_dither(ws2812_color_t c, uint8_t brightness,
                                         uint8_t bias)
{
    const uint16_t offset = (uint16_t)(32u + 64u * (bias % WS2812_DITHER_LEVELS));

    return (ws2812_color_t){
        .r = scale_channel_biased(c.r, brightness, offset),
        .g = scale_channel_biased(c.g, brightness, offset),
        .b = scale_channel_biased(c.b, brightness, offset),
        .w = scale_channel_biased(c.w, brightness, offset),
    };
}
