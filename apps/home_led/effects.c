#include "effects.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Tunables
 *
 * These are the aesthetic choices, not the mechanism: how fast the rainbow
 * turns, how long the wipe's tail is, how often a twinkle sparks. They are
 * deliberately here rather than in the profile -- a profile describes the
 * hardware, and none of these depend on it.
 * -------------------------------------------------------------------------*/

/* Rainbow: hue advanced per frame, and per LED along the strip. Six steps per
   LED spreads about one and a half turns of the wheel over 300 pixels. */
#define RAINBOW_STEP_PER_FRAME 9u
#define RAINBOW_STEP_PER_PIXEL 6u

/* Wipe: how far the head moves per frame, and how many pixels trail it. */
#define WIPE_STEP_PER_FRAME 1u
#define WIPE_TRAIL 6u

/* Twinkle: chance in 256 that a given pixel sparks this frame, and how far the
   unlit pixels are dimmed to leave the colour still readable underneath. */
#define TWINKLE_CHANCE 24u
#define TWINKLE_BACKGROUND_SHIFT 3u

/* Breathing: one full cycle, and the range it swings the brightness over.
   It never reaches zero -- a light that goes fully dark reads as a fault
   rather than as breathing. */
#define BREATHING_PERIOD_MS 8000u
#define BREATHING_MIN 180u
#define BREATHING_MAX 255u

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/*
 * A stateless hash, used to decide which pixels spark.
 *
 * Twinkle needs a value that is random-looking but reproducible from (pixel,
 * frame): keeping an actual PRNG would mean per-pixel state, and re-seeding it
 * each frame would make every pixel spark together. This is the usual
 * xor-shift-multiply mixer, which is plenty for deciding whether a pixel
 * sparkles.
 */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 17;
    x *= 0xED5AD4BBu;
    x ^= x >> 11;
    x *= 0xAC4C1B51u;
    x ^= x >> 15;
    x *= 0x31848BABu;
    x ^= x >> 14;
    return x;
}

/* Scale by `numerator`/255, for the places that dim a colour before the
   brightness scaling rather than instead of it. */
static ws2812_color_t shade(ws2812_color_t color, uint8_t numerator)
{
    return ws2812_color_scale(color, numerator);
}

static void fill_black(ws2812_color_t *pixels, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++) {
        pixels[i] = WS2812_COLOR_BLACK;
    }
}

/* ---------------------------------------------------------------------------
 * The effects
 * -------------------------------------------------------------------------*/

static void render_solid(const effects_t *effects, ws2812_color_t base,
                         uint8_t brightness, ws2812_color_t *pixels,
                         uint16_t length)
{
    for (uint16_t i = 0; i < length; i++) {
        const uint8_t bias = ws2812_dither_bias(i, 0, effects->frame);

        pixels[i] = ws2812_color_scale_dither(base, brightness, bias);
    }
}

static void render_rainbow(const effects_t *effects, uint8_t brightness,
                           ws2812_color_t *pixels, uint16_t length)
{
    /* The base hue is advanced once per frame in effects_render(); here it
       only spreads along the strip. from_hue16() wraps on its own, so the
       accumulation needs no modulus. */
    for (uint16_t i = 0; i < length; i++) {
        const uint16_t hue =
            (uint16_t)(effects->rainbow_hue + (uint16_t)(i * RAINBOW_STEP_PER_PIXEL));
        const ws2812_color_t color = ws2812_color_from_hue16(hue);
        const uint8_t bias = ws2812_dither_bias(i, 0, effects->frame);

        pixels[i] = ws2812_color_scale_dither(color, brightness, bias);
    }
}

static void render_twinkle(const effects_t *effects, ws2812_color_t base,
                           uint8_t brightness, ws2812_color_t *pixels,
                           uint16_t length)
{
    const ws2812_color_t background =
        ws2812_rgb((uint8_t)(base.r >> TWINKLE_BACKGROUND_SHIFT),
                   (uint8_t)(base.g >> TWINKLE_BACKGROUND_SHIFT),
                   (uint8_t)(base.b >> TWINKLE_BACKGROUND_SHIFT));

    for (uint16_t i = 0; i < length; i++) {
        /* Mixing the index by a large odd constant before the frame stops
           neighbouring pixels from sparking in step. */
        const uint32_t noise = hash32(((uint32_t)i * 2654435761u) ^ effects->frame);
        const uint8_t bias = ws2812_dither_bias(i, 0, effects->frame);

        if ((noise & 0xFFu) < TWINKLE_CHANCE) {
            /* Sparks vary in strength so the strip does not look like a row of
               identical dots switching on and off. */
            const uint8_t strength = (uint8_t)(192u + ((noise >> 8) & 0x3Fu));

            pixels[i] = ws2812_color_scale_dither(shade(base, strength), brightness, bias);
        } else {
            pixels[i] = ws2812_color_scale_dither(background, brightness, bias);
        }
    }
}

static void render_wipe(const effects_t *effects, ws2812_color_t base,
                        uint8_t brightness, ws2812_color_t *pixels,
                        uint16_t length)
{
    fill_black(pixels, length);

    const uint16_t head = (uint16_t)(effects->wipe_position % length);

    for (uint16_t t = 0; t < WIPE_TRAIL && t < length; t++) {
        /* Walk backwards from the head, wrapping round the end of the strip. */
        const uint16_t index = (uint16_t)((head + length - t) % length);

        /* Full at the head, fading to nothing at the tail. */
        const uint8_t fade = (uint8_t)((255u * (WIPE_TRAIL - t)) / WIPE_TRAIL);
        const uint8_t bias = ws2812_dither_bias(index, 0, effects->frame);

        pixels[index] = ws2812_color_scale_dither(shade(base, fade), brightness, bias);
    }
}

static void render_breathing(const effects_t *effects, ws2812_color_t base,
                             uint8_t brightness, ws2812_color_t *pixels,
                             uint16_t length, uint32_t now_ms)
{
    const uint32_t elapsed = (now_ms - effects->effect_start_ms) % BREATHING_PERIOD_MS;

    /*
     * A triangle through the period, eased -- which gives a curve with zero
     * slope at the trough, the peak, and the trough again, i.e. the shape of
     * a raised cosine without needing one. light_ease() is the same curve the
     * brightness fades use.
     */
    const uint32_t half = BREATHING_PERIOD_MS / 2u;
    const uint32_t rising = (elapsed < half) ? elapsed : (BREATHING_PERIOD_MS - elapsed);
    const uint16_t t = (uint16_t)((rising * LIGHT_EASE_ONE) / half);
    const uint32_t wave = light_ease(t > LIGHT_EASE_ONE ? (uint16_t)LIGHT_EASE_ONE : t);

    const uint32_t span = BREATHING_MAX - BREATHING_MIN;
    const uint8_t scale = (uint8_t)(BREATHING_MIN + (span * wave) / LIGHT_EASE_ONE);

    /* The wave modulates the light's brightness rather than replacing it, so
       a dim light breathes dimly instead of jumping to full. */
    const uint8_t effective = (uint8_t)(((uint16_t)brightness * scale) / 255u);

    for (uint16_t i = 0; i < length; i++) {
        const uint8_t bias = ws2812_dither_bias(i, 0, effects->frame);

        pixels[i] = ws2812_color_scale_dither(base, effective, bias);
    }
}

/* ---------------------------------------------------------------------------
 * Entry points
 * -------------------------------------------------------------------------*/

void effects_init(effects_t *effects, uint32_t now_ms)
{
    if (effects == NULL) {
        return;
    }

    memset(effects, 0, sizeof(*effects));
    effects->effect_start_ms = now_ms;
    effects->current = LIGHT_EFFECT_SOLID;
    effects->started = true;
}

void effects_render(effects_t *effects, const light_t *light,
                    ws2812_color_t *pixels, uint16_t length, uint32_t now_ms)
{
    if (effects == NULL || light == NULL || pixels == NULL || length == 0) {
        return;
    }

    if (!effects->started || effects->current != light->effect) {
        /* Start the new effect from its beginning rather than from wherever
           the previous one had got to: a wipe resuming mid-strip, or
           breathing resuming mid-exhale, looks like a glitch. */
        effects->current = light->effect;
        effects->effect_start_ms = now_ms;
        effects->rainbow_hue = 0;
        effects->wipe_position = 0;
        effects->started = true;
    }

    if (!light->on) {
        /* Dark, not frozen. The frame counter stops too, so the dither pattern
           resumes where it left off instead of jumping. */
        fill_black(pixels, length);
        return;
    }

    const uint8_t brightness = light_current_brightness(light);
    const ws2812_color_t base = light_current_color(light);

    switch (light->effect) {
        case LIGHT_EFFECT_RAINBOW:
            render_rainbow(effects, brightness, pixels, length);
            break;
        case LIGHT_EFFECT_TWINKLE:
            render_twinkle(effects, base, brightness, pixels, length);
            break;
        case LIGHT_EFFECT_WIPE:
            render_wipe(effects, base, brightness, pixels, length);
            break;
        case LIGHT_EFFECT_BREATHING:
            render_breathing(effects, base, brightness, pixels, length, now_ms);
            break;
        case LIGHT_EFFECT_SOLID:
        default:
            render_solid(effects, base, brightness, pixels, length);
            break;
    }

    /* Advance the animation only once a frame has actually been drawn, so the
       first frame of an effect is its frame zero. */
    effects->frame++;
    effects->rainbow_hue =
        (uint16_t)((effects->rainbow_hue + RAINBOW_STEP_PER_FRAME) % WS2812_HUE16_RANGE);
    effects->wipe_position = (uint16_t)(effects->wipe_position + WIPE_STEP_PER_FRAME);
}

void effects_render_test_pattern(ws2812_color_t *pixels, uint16_t length)
{
    if (pixels == NULL || length == 0) {
        return;
    }

    /* Dim on purpose: a full-white 300-pixel strip pulls far more current than
       a bench supply is likely to have, and this is the pattern most likely to
       be run on one. */
    for (uint16_t i = 0; i < length; i++) {
        pixels[i] = ws2812_rgb(8, 8, 8);
    }

    pixels[0] = ws2812_rgb(48, 0, 0);
    pixels[length - 1u] = ws2812_rgb(0, 48, 0);
}
