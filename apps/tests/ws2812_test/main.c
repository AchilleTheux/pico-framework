/*
 * ws2812_test - hardware test for the ws2812 component.
 *
 * Runs a fixed sequence of patterns and narrates each one over stdio, so the
 * strip can be checked against the printed description. See README.md beside
 * this file for wiring and the expected result.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "ws2812.h"

/* Overridable from profiles/tests/ws2812_test/*.cmake. */
#ifndef WS2812_TEST_PIN
#define WS2812_TEST_PIN 10
#endif

#ifndef WS2812_TEST_LENGTH
#define WS2812_TEST_LENGTH 16
#endif

#ifndef WS2812_TEST_IS_RGBW
#define WS2812_TEST_IS_RGBW 0
#endif

#define STEP_MS 1500

/* The application owns the pixel buffer; the component never allocates. */
static ws2812_color_t pixels[WS2812_TEST_LENGTH];
static ws2812_strip_t strip;

static void hold(const char *description)
{
    printf("  %s\n", description);
    ws2812_show(&strip);
    sleep_ms(STEP_MS);
}

static void test_solid_colors(void)
{
    static const struct {
        const char *name;
        ws2812_color_t color;
    } steps[] = {
        { "red   - if this looks green, the strip wants a different wire order", WS2812_COLOR_RED },
        { "green", WS2812_COLOR_GREEN },
        { "blue",  WS2812_COLOR_BLUE },
        { "white", WS2812_COLOR_WHITE },
    };

    puts("solid colours, whole strip:");
    for (unsigned i = 0; i < count_of(steps); i++) {
        ws2812_fill(&strip, steps[i].color);
        hold(steps[i].name);
    }
}

static void test_addressing(void)
{
    puts("single pixel walking from index 0 to the far end:");
    for (uint16_t i = 0; i < ws2812_length(&strip); i++) {
        ws2812_clear(&strip);
        ws2812_set_pixel(&strip, i, WS2812_COLOR_WHITE);
        ws2812_show(&strip);
        sleep_ms(80);
    }
    printf("  walked %u pixels\n", (unsigned)ws2812_length(&strip));
}

static void test_brightness(void)
{
    puts("brightness ramp on a white strip, 0 to full and back:");
    ws2812_fill(&strip, WS2812_COLOR_WHITE);
    for (int i = 0; i < 512; i++) {
        const int level = i < 256 ? i : 511 - i;
        ws2812_set_brightness(&strip, (uint8_t)level);
        ws2812_show(&strip);
        sleep_ms(4);
    }
    ws2812_set_brightness(&strip, 255);
}

static void test_rainbow(void)
{
    puts("rainbow sweeping along the strip:");
    for (unsigned frame = 0; frame < 256; frame++) {
        for (uint16_t i = 0; i < ws2812_length(&strip); i++) {
            const uint8_t hue = (uint8_t)(frame + (i * 256u) / ws2812_length(&strip));
            ws2812_set_pixel(&strip, i, ws2812_color_from_hsv(hue, 255, 255));
        }
        ws2812_show(&strip);
        sleep_ms(10);
    }
}

int main(void)
{
    stdio_init_all();

    /* Give a USB console a moment to attach before the first output. */
    sleep_ms(2000);

    const ws2812_config_t config = {
        .pio          = pio0,
        .pin          = WS2812_TEST_PIN,
        .pixels       = pixels,
        .length       = WS2812_TEST_LENGTH,
        .is_rgbw      = WS2812_TEST_IS_RGBW,
        .frequency_hz = WS2812_DEFAULT_FREQUENCY_HZ,
    };

    const ws2812_result_t result = ws2812_init(&strip, &config);
    if (result != WS2812_OK) {
        while (true) {
            printf("ws2812_init failed: %d\n", (int)result);
            sleep_ms(1000);
        }
    }

    printf("\nws2812_test: pin=%d length=%d rgbw=%d board=%s\n",
           WS2812_TEST_PIN, WS2812_TEST_LENGTH, WS2812_TEST_IS_RGBW, PICO_BOARD);

    unsigned pass = 0;
    while (true) {
        printf("\n--- pass %u ---\n", pass++);
        test_solid_colors();
        test_addressing();
        test_brightness();
        test_rainbow();

        ws2812_clear(&strip);
        ws2812_show(&strip);
        sleep_ms(1000);
    }
}
