/*
 * Host-side tests for the hardware-independent half of the ws2812 component.
 *
 * The wire-encoding tests in particular pin down a contract that is otherwise
 * only observable with an oscilloscope: the PIO program shifts left with a 24-
 * or 32-bit autopull threshold, so the payload must sit in the *high* bits and
 * the channel order on the wire is green, red, blue.
 */

#include "test.h"

#include "ws2812_color.h"

TEST(wire_encoding_is_left_aligned_grb)
{
    /* Distinct values per channel so a swapped order cannot pass. */
    const ws2812_color_t c = ws2812_rgb(0x11, 0x22, 0x33);

    CHECK_EQ_U32(ws2812_color_to_wire(c, false), 0x22113300u);
}

TEST(wire_encoding_leaves_the_low_byte_clear_for_rgb)
{
    /* The white channel must not reach the wire on a 24-bit strip, even when
       a caller leaves a stale value in it. */
    const ws2812_color_t c = ws2812_rgbw(0x11, 0x22, 0x33, 0xFF);

    CHECK_EQ_U32(ws2812_color_to_wire(c, false), 0x22113300u);
}

TEST(wire_encoding_appends_white_for_rgbw)
{
    const ws2812_color_t c = ws2812_rgbw(0x11, 0x22, 0x33, 0x44);

    CHECK_EQ_U32(ws2812_color_to_wire(c, true), 0x22113344u);
}

TEST(wire_encoding_of_black_is_zero)
{
    CHECK_EQ_U32(ws2812_color_to_wire(WS2812_COLOR_BLACK, false), 0u);
    CHECK_EQ_U32(ws2812_color_to_wire(WS2812_COLOR_BLACK, true), 0u);
}

TEST(wire_encoding_of_white_fills_the_used_bits)
{
    CHECK_EQ_U32(ws2812_color_to_wire(WS2812_COLOR_WHITE, false), 0xFFFFFF00u);
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_rgbw(255, 255, 255, 255), true),
                 0xFFFFFFFFu);
}

TEST(full_brightness_is_the_identity)
{
    const ws2812_color_t c = ws2812_rgbw(1, 127, 254, 255);
    const ws2812_color_t scaled = ws2812_color_scale(c, 255);

    CHECK_EQ_INT(scaled.r, c.r);
    CHECK_EQ_INT(scaled.g, c.g);
    CHECK_EQ_INT(scaled.b, c.b);
    CHECK_EQ_INT(scaled.w, c.w);
}

TEST(zero_brightness_is_black)
{
    const ws2812_color_t scaled = ws2812_color_scale(ws2812_rgbw(255, 255, 255, 255), 0);

    CHECK_EQ_U32(ws2812_color_to_wire(scaled, true), 0u);
}

TEST(half_brightness_halves_each_channel)
{
    const ws2812_color_t scaled = ws2812_color_scale(ws2812_rgb(255, 128, 10), 128);

    /* Round to nearest: 255*128/255 = 128, 128*128/255 = 64.3, 10*128/255 = 5.0 */
    CHECK_EQ_INT(scaled.r, 128);
    CHECK_EQ_INT(scaled.g, 64);
    CHECK_EQ_INT(scaled.b, 5);
}

TEST(brightness_is_monotonic_and_never_overflows)
{
    for (unsigned channel = 0; channel <= 255; channel++) {
        uint8_t previous = 0;
        for (unsigned level = 0; level <= 255; level++) {
            const ws2812_color_t scaled =
                ws2812_color_scale(ws2812_rgb((uint8_t)channel, 0, 0), (uint8_t)level);

            if (scaled.r < previous) {
                CHECK_EQ_INT(scaled.r, previous); /* reports the offending pair */
                return;
            }
            if (scaled.r > channel) {
                CHECK_EQ_INT(scaled.r, channel);
                return;
            }
            previous = scaled.r;
        }
    }
}

TEST(lerp_endpoints_are_exact)
{
    const ws2812_color_t from = ws2812_rgb(0, 200, 255);
    const ws2812_color_t to = ws2812_rgb(255, 10, 0);

    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_lerp(from, to, 0), false),
                 ws2812_color_to_wire(from, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_lerp(from, to, 255), false),
                 ws2812_color_to_wire(to, false));
}

TEST(lerp_midpoint_is_symmetric)
{
    /* Descending and ascending ramps must meet in the same place, which is
       what the sign-aware rounding in lerp_channel() is there for. */
    const ws2812_color_t a = ws2812_rgb(0, 0, 0);
    const ws2812_color_t b = ws2812_rgb(255, 100, 7);

    const ws2812_color_t forward = ws2812_color_lerp(a, b, 128);
    const ws2812_color_t backward = ws2812_color_lerp(b, a, 127);

    CHECK_EQ_INT(forward.r, backward.r);
    CHECK_EQ_INT(forward.g, backward.g);
    CHECK_EQ_INT(forward.b, backward.b);
}

TEST(hsv_with_no_saturation_is_grey)
{
    for (unsigned h = 0; h <= 255; h++) {
        const ws2812_color_t c = ws2812_color_from_hsv((uint8_t)h, 0, 90);
        if (c.r != 90 || c.g != 90 || c.b != 90) {
            CHECK_EQ_INT(c.r, 90);
            CHECK_EQ_INT(c.g, 90);
            CHECK_EQ_INT(c.b, 90);
            return;
        }
    }
}

TEST(hsv_with_no_value_is_black)
{
    for (unsigned h = 0; h <= 255; h++) {
        const ws2812_color_t c = ws2812_color_from_hsv((uint8_t)h, 255, 0);
        if (ws2812_color_to_wire(c, false) != 0) {
            CHECK_EQ_U32(ws2812_color_to_wire(c, false), 0u);
            return;
        }
    }
}

TEST(hsv_sector_boundaries_are_the_pure_colours)
{
    /* Each of the six 43-wide sectors starts on a primary or secondary. */
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(0, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_RED, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(43, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_YELLOW, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(86, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_GREEN, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(129, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_CYAN, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(172, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_BLUE, false));
    CHECK_EQ_U32(ws2812_color_to_wire(ws2812_color_from_hsv(215, 255, 255), false),
                 ws2812_color_to_wire(WS2812_COLOR_MAGENTA, false));
}

TEST(hsv_at_full_saturation_peaks_at_value)
{
    /* A fully saturated colour always has one channel at exactly v and one at
       0, whatever the hue. */
    for (unsigned h = 0; h <= 255; h++) {
        const ws2812_color_t c = ws2812_color_from_hsv((uint8_t)h, 255, 200);
        const uint8_t max = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
        const uint8_t min = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);

        if (max != 200 || min != 0) {
            CHECK_EQ_INT(max, 200);
            CHECK_EQ_INT(min, 0);
            return;
        }
    }
}

TEST_MAIN(
    RUN(wire_encoding_is_left_aligned_grb);
    RUN(wire_encoding_leaves_the_low_byte_clear_for_rgb);
    RUN(wire_encoding_appends_white_for_rgbw);
    RUN(wire_encoding_of_black_is_zero);
    RUN(wire_encoding_of_white_fills_the_used_bits);
    RUN(full_brightness_is_the_identity);
    RUN(zero_brightness_is_black);
    RUN(half_brightness_halves_each_channel);
    RUN(brightness_is_monotonic_and_never_overflows);
    RUN(lerp_endpoints_are_exact);
    RUN(lerp_midpoint_is_symmetric);
    RUN(hsv_with_no_saturation_is_grey);
    RUN(hsv_with_no_value_is_black);
    RUN(hsv_sector_boundaries_are_the_pure_colours);
    RUN(hsv_at_full_saturation_peaks_at_value);
)
