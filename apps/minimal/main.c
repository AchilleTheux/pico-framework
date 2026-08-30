/*
 * minimal - the smallest complete pico-framework application.
 *
 * Blinks the board LED and prints a line per second over stdio, using only
 * Pico SDK APIs. Pin and board facts come from the board header selected by
 * PICO_BOARD, never from this file.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "pico/version.h"

#define BLINK_INTERVAL_MS 500

/*
 * PICO_DEFAULT_LED_PIN is defined by the board header for boards with an
 * ordinary GPIO LED. Boards without one (or with a CYW43-driven LED, such as
 * pico_w) leave it undefined, so the blink is compiled out rather than
 * guessing at a pin.
 */
#ifdef PICO_DEFAULT_LED_PIN
#define MINIMAL_HAS_LED 1
#else
#define MINIMAL_HAS_LED 0
#endif

static void led_init(void)
{
#if MINIMAL_HAS_LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

static void led_set(bool on)
{
#if MINIMAL_HAS_LED
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)on;
#endif
}

int main(void)
{
    stdio_init_all();
    led_init();

    bool on = false;
    uint32_t tick = 0;

    while (true) {
        on = !on;
        led_set(on);

        if (on) {
            printf("pico-framework minimal: board=%s sdk=%s tick=%lu\n",
                   PICO_BOARD, PICO_SDK_VERSION_STRING, (unsigned long)tick++);
        }

        sleep_ms(BLINK_INTERVAL_MS);
    }
}
