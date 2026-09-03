/*
 * effects - what actually goes on the strip, for each of the light's effects.
 *
 * Renders into a pixel buffer the caller owns, rather than into a
 * ws2812_strip_t. That keeps every SDK call in main.c, so the whole of this
 * file compiles into the host tests -- and a rendering bug can be caught by
 * inspecting pixels instead of by staring at 300 LEDs.
 *
 * BRIGHTNESS AND GAMMA
 *
 * Effects apply brightness themselves, with dithering
 * (ws2812_color_scale_dither), and leave the strip's own
 * ws2812_set_brightness() at 255. Dithering *is* a scaling operation -- it
 * varies the rounding threshold as the value is scaled down -- so it has to
 * happen where the scaling happens, and the driver scales all pixels
 * identically by design.
 *
 * Gamma stays with the driver: ws2812_set_gamma() applies it after brightness,
 * as it must, and applying it here as well would correct twice. So the full
 * pipeline is: effect picks a colour, effect scales it with dither, driver
 * gamma-corrects it, wire.
 */

#ifndef HOME_LED_EFFECTS_H
#define HOME_LED_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "light.h"
#include "ws2812_color.h"

/*
 * Per-effect animation state.
 *
 * Kept in a caller-owned struct rather than in file statics, the same rule the
 * framework's components follow: two strips could then be animated
 * independently, and a test can render a deterministic sequence without
 * whatever ran before it leaking in.
 */
typedef struct {
    uint32_t frame;              /* advanced once per rendered frame */
    uint32_t effect_start_ms;    /* when the current effect began */
    light_effect_t current;      /* to notice the effect changing */
    bool started;

    uint16_t rainbow_hue;        /* wraps at WS2812_HUE16_RANGE */
    uint16_t wipe_position;
} effects_t;

void effects_init(effects_t *effects, uint32_t now_ms);

/*
 * Draw one frame.
 *
 * `pixels` must hold at least `length` entries; nothing else is touched. A
 * light that is off renders black rather than being skipped, so the strip
 * actually goes dark instead of freezing on its last frame.
 *
 * Selecting a different effect resets that effect's animation state -- a wipe
 * restarts from the top, breathing restarts from its trough -- rather than
 * resuming wherever the last one happened to be.
 */
void effects_render(effects_t *effects, const light_t *light,
                    ws2812_color_t *pixels, uint16_t length, uint32_t now_ms);

/*
 * A fixed pattern for checking wiring, independent of the light's state: the
 * first pixel red, the last green, everything between dim white. Which end of
 * the strip is which, how many pixels the firmware thinks there are, and
 * whether the colour order is right, all in one frame.
 */
void effects_render_test_pattern(ws2812_color_t *pixels, uint16_t length);

#endif /* HOME_LED_EFFECTS_H */
