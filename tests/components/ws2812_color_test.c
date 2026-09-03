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


/* ---------------------------------------------------------------------------
 * The 1536-step hue wheel
 * -------------------------------------------------------------------------*/

TEST(hue16_segment_boundaries_are_the_pure_colours)
{
    const struct {
        uint16_t hue;
        uint8_t r, g, b;
    } expected[] = {
        {    0, 255,   0,   0 },   /* red     */
        {  256, 255, 255,   0 },   /* yellow  */
        {  512,   0, 255,   0 },   /* green   */
        {  768,   0, 255, 255 },   /* cyan    */
        { 1024,   0,   0, 255 },   /* blue    */
        { 1280, 255,   0, 255 },   /* magenta */
    };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        const ws2812_color_t c = ws2812_color_from_hue16(expected[i].hue);

        CHECK_EQ_INT(c.r, expected[i].r);
        CHECK_EQ_INT(c.g, expected[i].g);
        CHECK_EQ_INT(c.b, expected[i].b);
        CHECK_EQ_INT(c.w, 0);
    }
}

TEST(hue16_wraps_at_the_end_of_the_wheel)
{
    /* An accumulator that runs past the end must land back where it started,
       which is the whole reason the wrap lives in here and not in callers. */
    const ws2812_color_t start = ws2812_color_from_hue16(0);
    const ws2812_color_t wrapped = ws2812_color_from_hue16(WS2812_HUE16_RANGE);
    const ws2812_color_t twice = ws2812_color_from_hue16(2u * WS2812_HUE16_RANGE + 100u);
    const ws2812_color_t once = ws2812_color_from_hue16(100u);

    CHECK_EQ_INT(wrapped.r, start.r);
    CHECK_EQ_INT(wrapped.g, start.g);
    CHECK_EQ_INT(wrapped.b, start.b);

    CHECK_EQ_INT(twice.r, once.r);
    CHECK_EQ_INT(twice.g, once.g);
    CHECK_EQ_INT(twice.b, once.b);
}

TEST(hue16_is_continuous_across_the_whole_wheel)
{
    /* No channel may jump by more than one step between adjacent hues,
       including across a segment boundary and across the wrap -- a jump is
       exactly what would show up as a band on a long strip. */
    ws2812_color_t previous = ws2812_color_from_hue16(0);

    for (uint16_t hue = 1; hue <= WS2812_HUE16_RANGE; hue++) {
        const ws2812_color_t c = ws2812_color_from_hue16(hue);
        const int dr = (int)c.r - (int)previous.r;
        const int dg = (int)c.g - (int)previous.g;
        const int db = (int)c.b - (int)previous.b;

        if (dr > 1 || dr < -1 || dg > 1 || dg < -1 || db > 1 || db < -1) {
            CHECK_EQ_INT(dr, 0);
            CHECK_EQ_INT(dg, 0);
            CHECK_EQ_INT(db, 0);
            return;
        }
        previous = c;
    }
}

TEST(hue16_is_always_fully_saturated)
{
    for (uint16_t hue = 0; hue < WS2812_HUE16_RANGE; hue++) {
        const ws2812_color_t c = ws2812_color_from_hue16(hue);
        const uint8_t max = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
        const uint8_t min = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);

        if (max != 255 || min != 0) {
            CHECK_EQ_INT(max, 255);
            CHECK_EQ_INT(min, 0);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Gamma
 * -------------------------------------------------------------------------*/

TEST(gamma_keeps_both_ends_of_the_range)
{
    /* Correction must not cost the strip its off state or its full output. */
    CHECK_EQ_INT(ws2812_gamma8(0), 0);
    CHECK_EQ_INT(ws2812_gamma8(255), 255);
}

TEST(gamma_is_monotonic)
{
    for (unsigned v = 1; v <= 255; v++) {
        if (ws2812_gamma8((uint8_t)v) < ws2812_gamma8((uint8_t)(v - 1))) {
            CHECK_EQ_INT(ws2812_gamma8((uint8_t)v), ws2812_gamma8((uint8_t)(v - 1)));
            return;
        }
    }
}

TEST(gamma_darkens_everything_between_the_ends)
{
    /* Gamma above 1 pulls the curve down; a table that brightened anywhere
       would be the wrong exponent, or inverted. */
    for (unsigned v = 1; v < 255; v++) {
        if (ws2812_gamma8((uint8_t)v) >= v) {
            CHECK_EQ_INT(ws2812_gamma8((uint8_t)v), (int)v - 1);
            return;
        }
    }
}

TEST(gamma_midpoint_matches_the_exponent)
{
    /* round(pow(128/255, 2.2) * 255) == 56. Pins the curve to gamma 2.2
       rather than to whatever shape happens to be monotonic. */
    CHECK_EQ_INT(ws2812_gamma8(128), 56);
    CHECK_EQ_INT(ws2812_gamma8(64), 12);
    CHECK_EQ_INT(ws2812_gamma8(192), 137);
}

TEST(gamma_applies_to_every_channel_including_white)
{
    const ws2812_color_t c = ws2812_color_gamma(ws2812_rgbw(128, 64, 192, 255));

    CHECK_EQ_INT(c.r, 56);
    CHECK_EQ_INT(c.g, 12);
    CHECK_EQ_INT(c.b, 137);
    CHECK_EQ_INT(c.w, 255);
}

TEST(gamma_table_and_function_agree)
{
    /* ws2812_set_gamma() takes the table directly, so the two must not be
       able to drift apart. */
    for (unsigned v = 0; v <= 255; v++) {
        if (ws2812_gamma_table[v] != ws2812_gamma8((uint8_t)v)) {
            CHECK_EQ_INT(ws2812_gamma_table[v], ws2812_gamma8((uint8_t)v));
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Ordered dithering
 * -------------------------------------------------------------------------*/

TEST(dither_bias_covers_every_level_over_a_2x2_block)
{
    bool seen[WS2812_DITHER_LEVELS] = { false };

    for (uint16_t y = 0; y < 2; y++) {
        for (uint16_t x = 0; x < 2; x++) {
            const uint8_t bias = ws2812_dither_bias(x, y, 0);

            CHECK(bias < WS2812_DITHER_LEVELS);
            seen[bias] = true;
        }
    }

    for (unsigned i = 0; i < WS2812_DITHER_LEVELS; i++) {
        CHECK(seen[i]);
    }
}

TEST(dither_bias_inverts_on_alternate_frames)
{
    /* A pattern that stood still would read as fixed texture rather than
       averaging away. Consecutive frames must sit on opposite sides. */
    for (uint16_t y = 0; y < 2; y++) {
        for (uint16_t x = 0; x < 2; x++) {
            const int even = ws2812_dither_bias(x, y, 0);
            const int odd = ws2812_dither_bias(x, y, 1);

            CHECK_EQ_INT(even + odd, (int)WS2812_DITHER_LEVELS - 1);
        }
    }
}

TEST(dither_averages_back_to_the_undithered_scale)
{
    /* The point of the whole exercise: the grain must cancel over a 2x2 block
       and two frames, not shift the colour. */
    for (unsigned brightness = 0; brightness <= 255; brightness += 5) {
        for (unsigned value = 0; value <= 255; value += 5) {
            const ws2812_color_t c = ws2812_rgb((uint8_t)value, (uint8_t)value,
                                                (uint8_t)value);
            unsigned total = 0;

            for (unsigned bias = 0; bias < WS2812_DITHER_LEVELS; bias++) {
                total += ws2812_color_scale_dither(c, (uint8_t)brightness,
                                                   (uint8_t)bias).r;
            }

            const unsigned plain = ws2812_color_scale(c, (uint8_t)brightness).r;
            const int mean_x4 = (int)total;
            const int plain_x4 = (int)plain * (int)WS2812_DITHER_LEVELS;

            if (mean_x4 - plain_x4 > (int)WS2812_DITHER_LEVELS ||
                plain_x4 - mean_x4 > (int)WS2812_DITHER_LEVELS) {
                CHECK_EQ_INT(mean_x4, plain_x4);
                return;
            }
        }
    }
}

TEST(dither_actually_varies_the_output_where_scaling_quantises)
{
    /*
     * 100 * 8 = 800, which is 3 + 35/255 of a step: too much to round away
     * and not enough to round up, so exactly one of the four thresholds must
     * cross. A value whose remainder falls below the finest threshold -- 130
     * at this brightness is 4 + 20/255 -- is correctly left alone by all
     * four, which is why the case here is chosen rather than arbitrary.
     */
    const ws2812_color_t c = ws2812_rgb(100, 100, 100);
    unsigned rounded_up = 0;

    for (unsigned bias = 0; bias < WS2812_DITHER_LEVELS; bias++) {
        if (ws2812_color_scale_dither(c, 8, (uint8_t)bias).r == 4) {
            rounded_up++;
        }
    }

    CHECK_EQ_INT((int)rounded_up, 1);
}

TEST(dither_recovers_depth_the_plain_scale_loses)
{
    /*
     * The claim the component makes, measured: at brightness 8 the plain
     * scale maps all 256 inputs onto a handful of outputs, and averaging the
     * four dithered results has to distinguish considerably more of them.
     * Without this, every test above would still pass for a dither that
     * merely jittered without adding information.
     */
    bool plain_seen[256] = { false };
    bool dithered_seen[4 * 256] = { false };
    unsigned plain_levels = 0;
    unsigned dithered_levels = 0;

    for (unsigned value = 0; value <= 255; value++) {
        const ws2812_color_t c = ws2812_rgb((uint8_t)value, 0, 0);
        unsigned total = 0;

        for (unsigned bias = 0; bias < WS2812_DITHER_LEVELS; bias++) {
            total += ws2812_color_scale_dither(c, 8, (uint8_t)bias).r;
        }

        const uint8_t plain = ws2812_color_scale(c, 8).r;

        if (!plain_seen[plain]) {
            plain_seen[plain] = true;
            plain_levels++;
        }
        if (!dithered_seen[total]) {
            dithered_seen[total] = true;
            dithered_levels++;
        }
    }

    CHECK(dithered_levels > plain_levels);
    CHECK(dithered_levels >= 3u * plain_levels);
}

TEST(dither_never_overflows_a_channel)
{
    for (unsigned bias = 0; bias < WS2812_DITHER_LEVELS; bias++) {
        const ws2812_color_t c =
            ws2812_color_scale_dither(ws2812_rgbw(255, 255, 255, 255), 255,
                                      (uint8_t)bias);

        CHECK_EQ_INT(c.r, 255);
        CHECK_EQ_INT(c.g, 255);
        CHECK_EQ_INT(c.b, 255);
        CHECK_EQ_INT(c.w, 255);
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
    RUN(hue16_segment_boundaries_are_the_pure_colours);
    RUN(hue16_wraps_at_the_end_of_the_wheel);
    RUN(hue16_is_continuous_across_the_whole_wheel);
    RUN(hue16_is_always_fully_saturated);
    RUN(gamma_keeps_both_ends_of_the_range);
    RUN(gamma_is_monotonic);
    RUN(gamma_darkens_everything_between_the_ends);
    RUN(gamma_midpoint_matches_the_exponent);
    RUN(gamma_applies_to_every_channel_including_white);
    RUN(gamma_table_and_function_agree);
    RUN(dither_bias_covers_every_level_over_a_2x2_block);
    RUN(dither_bias_inverts_on_alternate_frames);
    RUN(dither_averages_back_to_the_undithered_scale);
    RUN(dither_actually_varies_the_output_where_scaling_quantises);
    RUN(dither_recovers_depth_the_plain_scale_loses);
    RUN(dither_never_overflows_a_channel);
)
