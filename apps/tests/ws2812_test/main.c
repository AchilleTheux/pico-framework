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

/* Overridable from the profiles under profiles/tests/ws2812_test. */
#ifndef WS2812_TEST_PIN
#define WS2812_TEST_PIN 10
#endif

#ifndef WS2812_TEST_LENGTH
#define WS2812_TEST_LENGTH 16
#endif

#ifndef WS2812_TEST_IS_RGBW
#define WS2812_TEST_IS_RGBW 0
#endif

#ifndef WS2812_TEST_USE_DMA
#define WS2812_TEST_USE_DMA 0
#endif

#define STEP_MS 1500

/* The application owns the pixel buffer; the component never allocates. */
static ws2812_color_t pixels[WS2812_TEST_LENGTH];
static ws2812_strip_t strip;

#if WS2812_TEST_USE_DMA
/* The wire-format buffer that enables DMA. Separate from the pixels because
   the wire packing differs and brightness is applied on the way out. */
static uint32_t wire_buffer[WS2812_TEST_LENGTH];
#endif

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

#if WS2812_TEST_USE_DMA
/*
 * The point of DMA: measure how long the processor is actually held up.
 *
 * The blocking path waits for the frame to reach the strip, which is about
 * 30 us a pixel plus the latch gap. The async path returns as soon as the
 * frame has been copied into the wire buffer, so the difference is what a main
 * loop gets back to spend on something else.
 */
static void test_async_timing(void)
{
    puts("comparing blocking and DMA transmission:");

    ws2812_fill(&strip, WS2812_COLOR_BLUE);

    ws2812_wait(&strip);
    const uint64_t blocking_start = time_us_64();
    ws2812_show(&strip);
    const uint64_t blocking_us = time_us_64() - blocking_start;

    ws2812_wait(&strip);
    const uint64_t async_start = time_us_64();
    const ws2812_result_t started = ws2812_show_async(&strip);
    const uint64_t async_us = time_us_64() - async_start;

    printf("  blocking show returned after %llu us\n",
           (unsigned long long)blocking_us);
    printf("  async show returned after    %llu us (%s)\n",
           (unsigned long long)async_us,
           started == WS2812_OK ? "started" : "refused");

    /* The frame is still going out; prove is_busy says so, and that it clears. */
    const bool busy_immediately = ws2812_is_busy(&strip);
    ws2812_wait(&strip);
    const bool idle_after_wait = !ws2812_is_busy(&strip);

    printf("  busy straight after async: %s, idle after wait: %s\n",
           busy_immediately ? "yes" : "NO (unexpected)",
           idle_after_wait ? "yes" : "NO (unexpected)");

    /* A second frame while the first is in flight must be refused rather than
       corrupting what is on the wire. */
    ws2812_show_async(&strip);
    const ws2812_result_t second = ws2812_show_async(&strip);
    printf("  a frame during a frame: %s\n",
           second == WS2812_ERR_BUSY ? "refused, as it should be"
                                     : "ACCEPTED (unexpected)");
    ws2812_wait(&strip);
}

/* An animation driven the way a real main loop would: never blocking, just
   skipping a frame when the strip is not ready. */
static void test_async_animation(void)
{
    puts("rainbow driven without blocking, skipping frames when busy:");

    unsigned frames = 0;
    unsigned skipped = 0;
    const uint64_t until = time_us_64() + 3000000u;

    for (unsigned hue = 0; time_us_64() < until; hue++) {
        if (ws2812_is_busy(&strip)) {
            skipped++;
            continue;
        }
        for (uint16_t i = 0; i < ws2812_length(&strip); i++) {
            const uint8_t pixel_hue =
                (uint8_t)(hue + (i * 256u) / ws2812_length(&strip));
            ws2812_set_pixel(&strip, i, ws2812_color_from_hsv(pixel_hue, 255, 255));
        }
        if (ws2812_show_async(&strip) == WS2812_OK) {
            frames++;
        }
    }

    printf("  %u frames in 3 s (%u/s), %u polls found it busy\n",
           frames, frames / 3u, skipped);
    ws2812_wait(&strip);
}
#endif

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
#if WS2812_TEST_USE_DMA
        .wire_buffer  = wire_buffer,
#endif
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
#if WS2812_TEST_USE_DMA
        test_async_timing();
        test_async_animation();
#endif

        ws2812_clear(&strip);
        ws2812_show(&strip);
        sleep_ms(1000);
    }
}
