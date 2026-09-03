#include "light.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Effects and modes
 * -------------------------------------------------------------------------*/

/*
 * Indexed by light_effect_t, so the order here is the order Home Assistant
 * shows in its dropdown. These strings are what a controller sends back, so
 * changing one renames the effect as far as every controller is concerned.
 */
static const char *const effect_names[LIGHT_EFFECT_COUNT] = {
    "Solid",
    "Rainbow",
    "Twinkle",
    "Wipe",
    "Breathing",
};

const char *light_effect_name(light_effect_t effect)
{
    if ((unsigned)effect >= (unsigned)LIGHT_EFFECT_COUNT) {
        return NULL;
    }
    return effect_names[effect];
}

bool light_effect_from_name(const char *name, light_effect_t *out)
{
    if (name == NULL || out == NULL) {
        return false;
    }
    for (unsigned i = 0; i < (unsigned)LIGHT_EFFECT_COUNT; i++) {
        if (strcmp(name, effect_names[i]) == 0) {
            *out = (light_effect_t)i;
            return true;
        }
    }
    return false;
}

const char *light_color_mode_name(light_color_mode_t mode)
{
    /* The exact spellings Home Assistant's JSON light schema uses. */
    return (mode == LIGHT_COLOR_MODE_TEMP) ? "color_temp" : "rgb";
}

/* ---------------------------------------------------------------------------
 * Ramps
 * -------------------------------------------------------------------------*/

/*
 * Smoothstep, in fixed point: 3t^2 - 2t^3 over t in 0..SMOOTH_ONE.
 *
 * A linear fade has a visible corner at each end -- it starts and stops
 * abruptly even though the rate between is constant. Smoothstep eases both,
 * which is the difference between a light that fades and a light that ramps.
 *
 * The whole numerator is formed before the single division, in 64 bits.
 * Dividing at each step instead -- squaring, scaling down, cubing, scaling
 * down -- costs monotonicity: the truncations do not accumulate evenly, and
 * the result decreases by one at 42 points along the curve. That is a fade
 * that flickers backwards a step, which on a strip is a visible tick and is
 * exactly what `a_fade_never_goes_backwards_or_overshoots` caught.
 *
 * The largest intermediate is 3 * t^2 * SMOOTH_ONE at t = SMOOTH_ONE, which
 * is 3 * 2^30 -- past int32_t, comfortable in int64_t.
 */
#define SMOOTH_ONE ((int32_t)LIGHT_EASE_ONE)

uint16_t light_ease(uint16_t t)
{
    if (t == 0) {
        return 0;
    }
    if (t >= LIGHT_EASE_ONE) {
        return (uint16_t)LIGHT_EASE_ONE;
    }

    /* 3t^2 - 2t^3, scaled back down to 0..LIGHT_EASE_ONE in one step. */
    const int64_t square = (int64_t)t * t;
    const int64_t numerator = 3 * square * SMOOTH_ONE - 2 * square * t;

    return (uint16_t)(numerator / ((int64_t)SMOOTH_ONE * SMOOTH_ONE));
}

static int32_t smoothstep(int32_t t)
{
    if (t <= 0) {
        return 0;
    }
    if (t >= SMOOTH_ONE) {
        return SMOOTH_ONE;
    }
    return (int32_t)light_ease((uint16_t)t);
}

static void ramp_set(light_ramp_t *ramp, int32_t value)
{
    ramp->active = false;
    ramp->start_ms = 0;
    ramp->duration_ms = 0;
    ramp->from = value;
    ramp->to = value;
    ramp->current = value;
}

static void ramp_start(light_ramp_t *ramp, int32_t to, uint32_t duration_ms,
                       uint32_t now_ms)
{
    if (to == ramp->current || duration_ms == 0) {
        ramp_set(ramp, to);
        return;
    }

    ramp->active = true;
    ramp->start_ms = now_ms;
    ramp->duration_ms = duration_ms;
    ramp->from = ramp->current;
    ramp->to = to;
}

static void ramp_tick(light_ramp_t *ramp, uint32_t now_ms)
{
    if (!ramp->active) {
        return;
    }

    /*
     * Unsigned subtraction, so the millisecond counter wrapping past 2^32 --
     * a little under 50 days of uptime -- produces the correct elapsed time
     * rather than an enormous one that ends the fade instantly.
     */
    const uint32_t elapsed = now_ms - ramp->start_ms;

    if (elapsed >= ramp->duration_ms) {
        ramp_set(ramp, ramp->to);
        return;
    }

    const int32_t t = (int32_t)(((uint64_t)elapsed * SMOOTH_ONE) / ramp->duration_ms);
    const int32_t eased = smoothstep(t);

    ramp->current = ramp->from + (((ramp->to - ramp->from) * eased) / SMOOTH_ONE);
}

/* ---------------------------------------------------------------------------
 * The model
 * -------------------------------------------------------------------------*/

static void changed(light_t *light)
{
    light->generation++;
}

void light_init(light_t *light, uint32_t now_ms)
{
    if (light == NULL) {
        return;
    }

    memset(light, 0, sizeof(*light));

    light->on = false;
    light->brightness = LIGHT_DEFAULT_BRIGHTNESS;
    light->color = ws2812_rgb(255, 255, 255);
    light->mireds = LIGHT_DEFAULT_MIREDS;
    light->color_mode = LIGHT_COLOR_MODE_TEMP;
    light->effect = LIGHT_EFFECT_SOLID;

    /* Both fades start finished: the first frame shows the stored settings
       rather than ramping up to them from nothing. */
    ramp_set(&light->brightness_ramp, light->brightness);
    ramp_set(&light->mireds_ramp, light->mireds);

    light->generation = 1;
    (void)now_ms;
}

void light_set_power(light_t *light, bool on, uint32_t now_ms)
{
    if (light == NULL || light->on == on) {
        return;
    }

    /*
     * Power is separate from the effect, so switching off and back on resumes
     * whatever was running rather than resetting to solid. It takes effect at
     * once: a controller that asked for off expects off, not a fade.
     */
    light->on = on;
    changed(light);
    (void)now_ms;
}

void light_set_brightness(light_t *light, uint8_t brightness, uint32_t now_ms)
{
    if (light == NULL || light->brightness == brightness) {
        return;
    }

    light->brightness = brightness;
    ramp_start(&light->brightness_ramp, brightness, LIGHT_FADE_BRIGHTNESS_MS, now_ms);
    changed(light);
}

void light_set_color(light_t *light, ws2812_color_t color, uint32_t now_ms)
{
    if (light == NULL) {
        return;
    }

    const bool same = light->color_mode == LIGHT_COLOR_MODE_RGB &&
                      light->color.r == color.r &&
                      light->color.g == color.g &&
                      light->color.b == color.b;
    if (same) {
        return;
    }

    light->color = ws2812_rgb(color.r, color.g, color.b);
    light->color_mode = LIGHT_COLOR_MODE_RGB;
    changed(light);
    (void)now_ms;
}

void light_set_mireds(light_t *light, uint16_t mireds, uint32_t now_ms)
{
    if (light == NULL) {
        return;
    }

    /* Clamp rather than refuse: a controller asking for 600 wants the warmest
       this light has, and saying no leaves it showing something else. */
    if (mireds < LIGHT_MIREDS_MIN) {
        mireds = LIGHT_MIREDS_MIN;
    } else if (mireds > LIGHT_MIREDS_MAX) {
        mireds = LIGHT_MIREDS_MAX;
    }

    if (light->color_mode == LIGHT_COLOR_MODE_TEMP && light->mireds == mireds) {
        return;
    }

    light->mireds = mireds;
    light->color_mode = LIGHT_COLOR_MODE_TEMP;
    ramp_start(&light->mireds_ramp, mireds, LIGHT_FADE_MIREDS_MS, now_ms);
    changed(light);
}

bool light_set_effect(light_t *light, light_effect_t effect, uint32_t now_ms)
{
    if (light == NULL || (unsigned)effect >= (unsigned)LIGHT_EFFECT_COUNT) {
        return false;
    }
    if (light->effect == effect) {
        return true;
    }

    light->effect = effect;
    changed(light);
    (void)now_ms;
    return true;
}

void light_capture(const light_t *light, light_settings_t *out)
{
    if (light == NULL || out == NULL) {
        return;
    }

    out->on = light->on;
    out->brightness = light->brightness;
    out->color = light->color;
    out->mireds = light->mireds;
    out->color_mode = light->color_mode;
    out->effect = light->effect;
}

void light_restore(light_t *light, const light_settings_t *settings, uint32_t now_ms)
{
    if (light == NULL) {
        return;
    }

    light_init(light, now_ms);
    if (settings == NULL) {
        return;
    }

    light->on = settings->on;
    light->brightness = settings->brightness;
    light->color = ws2812_rgb(settings->color.r, settings->color.g, settings->color.b);

    /* Out of range means the stored copy is not trustworthy, so fall back
       rather than propagating it into a table index or a colour curve. */
    light->mireds = (settings->mireds >= LIGHT_MIREDS_MIN &&
                     settings->mireds <= LIGHT_MIREDS_MAX)
        ? settings->mireds
        : (uint16_t)LIGHT_DEFAULT_MIREDS;
    light->color_mode = (settings->color_mode == LIGHT_COLOR_MODE_RGB)
        ? LIGHT_COLOR_MODE_RGB
        : LIGHT_COLOR_MODE_TEMP;
    light->effect = ((unsigned)settings->effect < (unsigned)LIGHT_EFFECT_COUNT)
        ? settings->effect
        : LIGHT_EFFECT_SOLID;

    /* Finished, not running: the strip lights at the stored values rather than
       fading up to them from whatever light_init() left behind. */
    ramp_set(&light->brightness_ramp, light->brightness);
    ramp_set(&light->mireds_ramp, light->mireds);

    /* One generation, for the whole restore. The caller compares this against
       what it has already saved and published, and a restore is neither. */
    light->generation = 1;
}

void light_tick(light_t *light, uint32_t now_ms)
{
    if (light == NULL) {
        return;
    }
    ramp_tick(&light->brightness_ramp, now_ms);
    ramp_tick(&light->mireds_ramp, now_ms);
}

bool light_is_fading(const light_t *light)
{
    return light != NULL &&
           (light->brightness_ramp.active || light->mireds_ramp.active);
}

uint8_t light_current_brightness(const light_t *light)
{
    if (light == NULL) {
        return 0;
    }

    const int32_t value = light->brightness_ramp.current;

    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

uint16_t light_current_mireds(const light_t *light)
{
    if (light == NULL) {
        return LIGHT_MIREDS_MIN;
    }

    const int32_t value = light->mireds_ramp.current;

    if (value < (int32_t)LIGHT_MIREDS_MIN) {
        return LIGHT_MIREDS_MIN;
    }
    if (value > (int32_t)LIGHT_MIREDS_MAX) {
        return LIGHT_MIREDS_MAX;
    }
    return (uint16_t)value;
}

ws2812_color_t light_current_color(const light_t *light)
{
    if (light == NULL) {
        return WS2812_COLOR_BLACK;
    }
    if (light->color_mode == LIGHT_COLOR_MODE_TEMP) {
        return light_color_from_mireds(light_current_mireds(light));
    }
    return light->color;
}

/* ---------------------------------------------------------------------------
 * Colour temperature
 * -------------------------------------------------------------------------*/

/*
 * The black-body curve, sampled every 25 mireds from 150 to 500 -- about
 * 6666 K down to 2000 K, which covers Home Assistant's 153..500 range with a
 * sample either side.
 *
 * Sampling in mired space rather than kelvin is what keeps the interpolation
 * uniform: mireds are what the controller sends and what the fade ramps
 * through, so even steps there are even steps here.
 */
#define MIREDS_TABLE_BASE 150u
#define MIREDS_TABLE_STEP 25u
#define MIREDS_TABLE_COUNT 15u

static const uint8_t mireds_table[MIREDS_TABLE_COUNT][3] = {
    { 255, 250, 255 },   /* 150 mireds, ~6666 K */
    { 255, 241, 229 },   /* 175 mireds, ~5714 K */
    { 255, 228, 206 },   /* 200 mireds, ~5000 K */
    { 255, 216, 185 },   /* 225 mireds, ~4444 K */
    { 255, 206, 166 },   /* 250 mireds, ~4000 K */
    { 255, 196, 148 },   /* 275 mireds, ~3636 K */
    { 255, 188, 131 },   /* 300 mireds, ~3333 K */
    { 255, 180, 115 },   /* 325 mireds, ~3076 K */
    { 255, 172, 100 },   /* 350 mireds, ~2857 K */
    { 255, 165,  85 },   /* 375 mireds, ~2666 K */
    { 255, 159,  70 },   /* 400 mireds, ~2500 K */
    { 255, 153,  56 },   /* 425 mireds, ~2352 K */
    { 255, 147,  42 },   /* 450 mireds, ~2222 K */
    { 255, 142,  28 },   /* 475 mireds, ~2105 K */
    { 255, 137,  14 },   /* 500 mireds, ~2000 K */
};

static uint8_t interpolate(uint8_t from, uint8_t to, uint32_t position, uint32_t span)
{
    const int32_t delta = (int32_t)to - (int32_t)from;

    return (uint8_t)((int32_t)from + (delta * (int32_t)position) / (int32_t)span);
}

ws2812_color_t light_color_from_mireds(uint16_t mireds)
{
    if (mireds < LIGHT_MIREDS_MIN) {
        mireds = LIGHT_MIREDS_MIN;
    } else if (mireds > LIGHT_MIREDS_MAX) {
        mireds = LIGHT_MIREDS_MAX;
    }

    const uint32_t offset = (uint32_t)mireds - MIREDS_TABLE_BASE;
    uint32_t index = offset / MIREDS_TABLE_STEP;
    const uint32_t position = offset % MIREDS_TABLE_STEP;

    if (index >= MIREDS_TABLE_COUNT - 1u) {
        /* The last sample is exactly LIGHT_MIREDS_MAX, so there is nothing
           beyond it to interpolate towards. */
        const uint8_t *const last = mireds_table[MIREDS_TABLE_COUNT - 1u];
        return ws2812_rgb(last[0], last[1], last[2]);
    }

    const uint8_t *const low = mireds_table[index];
    const uint8_t *const high = mireds_table[index + 1u];

    return ws2812_rgb(interpolate(low[0], high[0], position, MIREDS_TABLE_STEP),
                      interpolate(low[1], high[1], position, MIREDS_TABLE_STEP),
                      interpolate(low[2], high[2], position, MIREDS_TABLE_STEP));
}
