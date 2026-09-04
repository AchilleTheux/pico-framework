/*
 * home_led - a WS2812 / WS2815 strip as a Home Assistant light.
 *
 * This file is startup and the loop, and nothing else. The work is split by
 * what it needs rather than by what it does:
 *
 *   light.c     the model -- power, brightness, colour, effect, fades
 *   led_range.c the inclusive section of the strip selected for rendering
 *   effects.c   what a frame looks like
 *   ha.c        the Home Assistant topics, discovery document and JSON
 *   settings.c  what survives a power cut, and the commands that set it
 *   net.c       the link, the broker session, and the announcement
 *   render.c    frames onto the strip, and the onboard LED
 *   console.c   the command table and the interpreter
 *
 * The first four call no Pico SDK function, so they compile into the host
 * tests. The rest is wiring, and shares one app_t rather than a drawer of
 * file statics -- see app.h.
 *
 * Nothing is compiled in: WiFi credentials, the broker, the device id and the
 * light's own state all live in flash and are typed in once over the console
 * (DESIGN_DOC.md section 13). See README.md for that session.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "app.h"
#include "console.h"
#include "net.h"
#include "render.h"
#include "settings.h"

/*
 * The one instance. Static rather than automatic because it holds the pixel
 * and wire buffers -- 2.4 KiB at 300 LEDs, which has no business on the
 * startup stack -- and because DMA reads from it for as long as the board
 * runs.
 */
static app_t app;

int main(void)
{
    stdio_init_all();

    const uint32_t started_ms = app_now_ms();

    settings_load(&app, started_ms);
    app.saved_generation = app.light.generation;
    app.saved_range_generation = app.range.generation;
    app.published_generation = app.light.generation;
    app.published_range_generation = app.range.generation;
    app.seen_generation = app.light.generation;
    app.seen_range_generation = app.range.generation;
    app.last_change_ms = started_ms;

    effects_init(&app.effects, started_ms);
    render_init(&app);

    /* Clear pixels that may still be latched from before a soft reboot. Do
       this before waiting for the USB console, so the requested boot-off
       state takes effect immediately rather than two seconds later. */
    render_frame(&app, started_ms);

    sleep_ms(2000);

    if (!console_init(&app)) {
        /* A table that did not fit, or a stream the interpreter refused.
           Neither is recoverable, and neither can be reported through the
           console that just failed to start. */
        while (true) {
            printf("console_init failed\n");
            sleep_ms(1000);
        }
    }

    console_banner(&app);

    /* After the banner, because net_init() reports its own failures through
       the console. */
    net_init(&app);
    render_status_led_off(&app);
    net_start_if_configured(&app);

    uint32_t last_frame_ms = started_ms;

    while (true) {
        cli_poll(&app.cli);
        net_poll(&app);

        /* Polling can invoke a command callback, and those callbacks take
           their own current timestamp when they start a ramp. Sample after
           them so light_tick() can never immediately receive a timestamp
           older than the ramp it is advancing. */
        const uint32_t now_ms = app_now_ms();
        light_tick(&app.light, now_ms);

        if (now_ms - last_frame_ms >= FRAME_INTERVAL_MS) {
            last_frame_ms = now_ms;
            render_frame(&app, now_ms);
        }

        /*
         * Note when the light or range last changed, so the flash write can wait for
         * the fiddling to stop.
         *
         * Driven by the generation counters themselves rather than by what has
         * been published: net_publish_state() does nothing while there is no
         * broker session, so keying this off it would restart the timer on
         * every loop and a board running from the console alone would never
         * save anything at all.
         */
        if (app.light.generation != app.seen_generation ||
            app.range.generation != app.seen_range_generation) {
            app.seen_generation = app.light.generation;
            app.seen_range_generation = app.range.generation;
            app.last_change_ms = now_ms;
        }

        if ((app.light.generation != app.published_generation ||
             app.range.generation != app.published_range_generation) &&
            now_ms - app.last_publish_ms >= PUBLISH_INTERVAL_MS) {
            net_publish_state(&app);
        }

        if ((app.light.generation != app.saved_generation ||
             app.range.generation != app.saved_range_generation) &&
            now_ms - app.last_change_ms >= SAVE_QUIET_MS) {
            if (settings_store(&app)) {
                app.saved_generation = app.light.generation;
                app.saved_range_generation = app.range.generation;
            } else {
                /* Try again after another quiet period rather than spinning
                   on a failing flash write every loop. */
                app.last_change_ms = now_ms;
            }
        }
    }
}
