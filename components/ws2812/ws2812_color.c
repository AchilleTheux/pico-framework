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
