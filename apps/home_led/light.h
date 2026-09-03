/*
 * light - what this firmware is actually controlling, independent of how it is
 * told to.
 *
 * Home Assistant drives it over MQTT and a console drives it over USB; neither
 * appears here. This is the model both of them talk to: on or off, how bright,
 * a colour given either as RGB or as a colour temperature, and which effect is
 * running. `ha.h` translates one protocol into these calls and `effects.h`
 * reads the result to decide what to put on the strip.
 *
 * NO PICO SDK
 *
 * Nothing in here calls the SDK -- time arrives as a `now_ms` argument rather
 * than being read from a timer -- so the whole model compiles into the host
 * tests, the same split ws2812.c and ws2812_color.c already use
 * (DESIGN_DOC.md section 19). A fade that only runs correctly on hardware is a
 * fade nobody can test.
 *
 * TRANSITIONS
 *
 * Brightness and colour temperature move gradually rather than jumping,
 * because a light that snaps between levels reads as a fault. Both are driven
 * by light_tick() from the main loop and neither blocks anything.
 */

#ifndef HOME_LED_LIGHT_H
#define HOME_LED_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

#include "ws2812_color.h"

/*
 * How long a change takes to arrive. Colour temperature is slower than
 * brightness on purpose: it is a bigger perceptual jump, and hurrying it looks
 * like the light changed its mind.
 */
#ifndef LIGHT_FADE_BRIGHTNESS_MS
#define LIGHT_FADE_BRIGHTNESS_MS 3000u
#endif

#ifndef LIGHT_FADE_MIREDS_MS
#define LIGHT_FADE_MIREDS_MS 4000u
#endif

/*
 * The range Home Assistant uses for colour temperature, in mireds
 * (1000000 / kelvin, so a *larger* number is a warmer light). 153 is about
 * 6500 K and 500 is about 2000 K.
 */
#define LIGHT_MIREDS_MIN 153u
#define LIGHT_MIREDS_MAX 500u

/*
 * The effects offered, and the order they appear in the Home Assistant
 * effect list. Adding one means adding a name here, a renderer in effects.c,
 * and nothing else -- LIGHT_EFFECT_COUNT sizes everything that iterates.
 */
typedef enum {
    LIGHT_EFFECT_SOLID = 0,
    LIGHT_EFFECT_RAINBOW,
    LIGHT_EFFECT_TWINKLE,
    LIGHT_EFFECT_WIPE,
    LIGHT_EFFECT_BREATHING,
    LIGHT_EFFECT_COUNT,
} light_effect_t;

/* The name Home Assistant shows and sends back. NULL for an out-of-range
   effect, so a caller cannot print past the end of the table. */
const char *light_effect_name(light_effect_t effect);

/* Match a name from a command payload. Case-sensitive, because that is what
   Home Assistant echoes back from the list this device published. */
bool light_effect_from_name(const char *name, light_effect_t *out);

/*
 * Whether the colour comes from RGB or from a colour temperature. Home
 * Assistant treats these as mutually exclusive modes rather than as two
 * settings that are both live, and so does this.
 */
typedef enum {
    LIGHT_COLOR_MODE_RGB = 0,
    LIGHT_COLOR_MODE_TEMP,
} light_color_mode_t;

const char *light_color_mode_name(light_color_mode_t mode);

/*
 * One value moving from where it was to where it has been asked to go.
 *
 * Values, not pointers: the original this was ported from kept a `int *` and a
 * `uint8_t *` in the same struct with a tag saying which was live, and wrote
 * four bytes through a pointer aimed at a `uint16_t`. Returning the current
 * value instead means there is nothing to alias and nothing to get wrong.
 */
typedef struct {
    bool active;
    uint32_t start_ms;
    uint32_t duration_ms;
    int32_t from;
    int32_t to;
    int32_t current;
} light_ramp_t;

typedef struct {
    /* Where the light has been asked to be. */
    bool on;
    uint8_t brightness;
    ws2812_color_t color;
    uint16_t mireds;
    light_color_mode_t color_mode;
    light_effect_t effect;

    /* Where it currently is, which lags the above while a fade runs. */
    light_ramp_t brightness_ramp;
    light_ramp_t mireds_ramp;

    /*
     * Bumped by every call that changes anything a controller can observe.
     * The application watches it to decide when to publish a new state and
     * when the stored settings are worth rewriting, instead of each setter
     * having to remember to ask for both.
     */
    uint32_t generation;
} light_t;

/*
 * A sane light on a board that has never been configured: on, mid brightness,
 * warm white, solid. Both fades start already finished, so the first frame
 * rendered is the requested one rather than a ramp up from black.
 */
void light_init(light_t *light, uint32_t now_ms);

void light_set_power(light_t *light, bool on, uint32_t now_ms);
void light_set_brightness(light_t *light, uint8_t brightness, uint32_t now_ms);

/* Selects RGB mode. */
void light_set_color(light_t *light, ws2812_color_t color, uint32_t now_ms);

/* Selects colour-temperature mode. Values outside LIGHT_MIREDS_MIN..MAX are
   clamped rather than refused -- a controller sending 600 wants the warmest
   the light has, not an error. */
void light_set_mireds(light_t *light, uint16_t mireds, uint32_t now_ms);

/* False for an out-of-range effect, leaving the current one running. */
bool light_set_effect(light_t *light, light_effect_t effect, uint32_t now_ms);

/*
 * Advance both fades. Call it every time round the main loop; it is cheap and
 * doing it less often only makes the fades coarser.
 */
void light_tick(light_t *light, uint32_t now_ms);

/* True while either fade is still running -- the light is not yet showing what
   it was last asked for. */
bool light_is_fading(const light_t *light);

/* ---------------------------------------------------------------------------
 * Saving and restoring
 * -------------------------------------------------------------------------*/

/*
 * Everything worth surviving a power cut -- which is the settings, not the
 * fades. A light coming back should already be where it was, not ramping
 * towards it.
 */
typedef struct {
    bool on;
    uint8_t brightness;
    ws2812_color_t color;
    uint16_t mireds;
    light_color_mode_t color_mode;
    light_effect_t effect;
} light_settings_t;

void light_capture(const light_t *light, light_settings_t *out);

/*
 * Adopt stored settings wholesale, with both fades already finished so the
 * first frame is the stored one.
 *
 * Values are validated on the way in, not trusted: this data comes from flash,
 * which can hold whatever a half-finished write or an older firmware left
 * there, and an effect index past the end of the table would be a jump through
 * a bad pointer. Anything out of range falls back to a sane default rather
 * than being refused -- a light that will not start because one stored byte is
 * wrong is worse than one that starts white.
 */
void light_restore(light_t *light, const light_settings_t *settings, uint32_t now_ms);

/* ---------------------------------------------------------------------------
 * What to render right now
 * -------------------------------------------------------------------------*/

/* The faded brightness, not the requested one. */
uint8_t light_current_brightness(const light_t *light);

/* The faded colour temperature, not the requested one. */
uint16_t light_current_mireds(const light_t *light);

/*
 * The colour to draw with at full brightness: the RGB that was set, or the
 * current colour temperature converted to one. Effects scale this themselves
 * rather than getting a pre-dimmed colour, because dithering has to happen at
 * the point of scaling.
 */
ws2812_color_t light_current_color(const light_t *light);

/* ---------------------------------------------------------------------------
 * Easing
 * -------------------------------------------------------------------------*/

/*
 * Smoothstep in fixed point: 3t^2 - 2t^3, over 0..LIGHT_EASE_ONE.
 *
 * The fades above use it, and effects.c uses it for the breathing wave, which
 * wants exactly the same shape -- zero slope at both ends, steepest in the
 * middle. Exposed rather than duplicated because two copies of a fixed-point
 * curve are two chances to get the rounding wrong in different ways.
 *
 * Monotonic across the whole range. That is not free: forming the numerator in
 * stages and dividing at each one loses it at a few dozen points, which on a
 * strip is a fade that ticks backwards.
 */
#define LIGHT_EASE_ONE 1024u

uint16_t light_ease(uint16_t t);

/* ---------------------------------------------------------------------------
 * Colour temperature
 * -------------------------------------------------------------------------*/

/*
 * Mireds to RGB, by table and interpolation.
 *
 * The usual approximation for this is a pair of logarithms and a power,
 * evaluated per pixel per frame. Neither RP2040 nor RP2350 has hardware
 * floating point, and the answer only has to be right to a shade the eye
 * cannot name -- so this samples the same curve at 2000..6500 K and
 * interpolates between the samples. Integer throughout, and exact at the
 * sample points, which is what the host test pins.
 */
ws2812_color_t light_color_from_mireds(uint16_t mireds);

#endif /* HOME_LED_LIGHT_H */
