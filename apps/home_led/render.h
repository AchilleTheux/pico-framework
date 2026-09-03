/*
 * render - putting frames on the strip, and controlling the board's own LED.
 *
 * The strip half of the application: claim a state machine, and once every
 * FRAME_INTERVAL_MS hand effects.c a buffer and hand the result to DMA.
 *
 * Deliberately thin. Everything about what a frame should look like lives in
 * effects.c, where it can be host-tested; what is left here is the part that
 * needs hardware.
 */

#ifndef HOME_LED_RENDER_H
#define HOME_LED_RENDER_H

#include "app.h"

/*
 * Claim a PIO state machine and configure the strip, including the wire
 * colour order the build asked for.
 *
 * Sets app->strip_ready. A failure is not fatal: the console still comes up,
 * which is the only way anyone would find out what went wrong.
 */
void render_init(app_t *app);

/*
 * Draw and send one frame, or skip if the previous one is still going out.
 * Skipping is the right answer for an animation -- the next frame is 20 ms
 * away and will be more current than this one.
 */
void render_frame(app_t *app, uint32_t now_ms);

/* Explicitly turn off the radio module's onboard LED once the CYW43 is ready. */
void render_status_led_off(app_t *app);

#endif /* HOME_LED_RENDER_H */
