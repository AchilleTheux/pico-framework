/*
 * Host-side tests for the home_led light model.
 *
 * The fades are the part worth testing here. They are the one piece of this
 * application whose behaviour is invisible in a single frame -- a fade that
 * ends early, overshoots, or dies at the millisecond counter's wrap looks like
 * a hardware fault and would be chased as one.
 */

#include "test.h"

#include "light.h"

/* Long enough to be past any fade in this file. */
#define WELL_PAST 20000u

static void settle(light_t *light, uint32_t at_ms)
{
    light_tick(light, at_ms);
}

/* ---------------------------------------------------------------------------
 * Effects
 * -------------------------------------------------------------------------*/

TEST(effect_names_round_trip)
{
    for (unsigned i = 0; i < (unsigned)LIGHT_EFFECT_COUNT; i++) {
        const char *name = light_effect_name((light_effect_t)i);
        light_effect_t back;

        CHECK(name != NULL);
        CHECK(light_effect_from_name(name, &back));
        CHECK_EQ_INT((int)back, (int)i);
    }
}

TEST(the_effect_list_is_the_one_home_assistant_was_told_about)
{
    /* These strings are a published interface: a controller sends back
       whatever this device advertised, so renaming one silently breaks it. */
    CHECK_EQ_STR(light_effect_name(LIGHT_EFFECT_SOLID), "Solid");
    CHECK_EQ_STR(light_effect_name(LIGHT_EFFECT_RAINBOW), "Rainbow");
    CHECK_EQ_STR(light_effect_name(LIGHT_EFFECT_TWINKLE), "Twinkle");
    CHECK_EQ_STR(light_effect_name(LIGHT_EFFECT_WIPE), "Wipe");
    CHECK_EQ_STR(light_effect_name(LIGHT_EFFECT_BREATHING), "Breathing");
    CHECK_EQ_INT((int)LIGHT_EFFECT_COUNT, 5);
}

TEST(an_unknown_effect_name_is_refused)
{
    light_effect_t effect = LIGHT_EFFECT_RAINBOW;

    CHECK(!light_effect_from_name("Strobe", &effect));
    CHECK(!light_effect_from_name("", &effect));
    /* Case matters: Home Assistant echoes the exact string it was given. */
    CHECK(!light_effect_from_name("solid", &effect));
    CHECK_EQ_INT((int)effect, (int)LIGHT_EFFECT_RAINBOW);

    CHECK(light_effect_name(LIGHT_EFFECT_COUNT) == NULL);
    CHECK(light_effect_name((light_effect_t)99) == NULL);
}

TEST(setting_an_out_of_range_effect_leaves_the_current_one_running)
{
    light_t light;

    light_init(&light, 0);
    CHECK(light_set_effect(&light, LIGHT_EFFECT_WIPE, 0));
    CHECK(!light_set_effect(&light, (light_effect_t)42, 0));
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_WIPE);
}

/* ---------------------------------------------------------------------------
 * Defaults
 * -------------------------------------------------------------------------*/

TEST(a_fresh_light_starts_off_at_solid_3000k)
{
    light_t light;

    light_init(&light, 12345);

    CHECK(!light.on);
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_SOLID);
    CHECK_EQ_INT((int)light.color_mode, (int)LIGHT_COLOR_MODE_TEMP);
    CHECK_EQ_INT(light.mireds, LIGHT_DEFAULT_MIREDS);

    /* Both fades start finished, so the very first frame shows the settings
       rather than ramping up to them from black. */
    CHECK(!light_is_fading(&light));
    CHECK_EQ_INT(light_current_brightness(&light), light.brightness);
    CHECK_EQ_INT(light_current_mireds(&light), light.mireds);
}

/* ---------------------------------------------------------------------------
 * Brightness fade
 * -------------------------------------------------------------------------*/

TEST(a_brightness_change_arrives_gradually_and_completely)
{
    light_t light;

    light_init(&light, 0);
    light_set_brightness(&light, 0, 0);
    settle(&light, WELL_PAST);
    CHECK_EQ_INT(light_current_brightness(&light), 0);

    light_set_brightness(&light, 200, WELL_PAST);
    CHECK(light_is_fading(&light));

    /* Still near the start a moment in. */
    settle(&light, WELL_PAST + 1);
    CHECK(light_current_brightness(&light) < 5);

    /* Somewhere in between, part way. */
    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS / 2u);
    const uint8_t middle = light_current_brightness(&light);
    CHECK(middle > 20);
    CHECK(middle < 180);

    /* Exactly on target at the end, not one step short. */
    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS);
    CHECK_EQ_INT(light_current_brightness(&light), 200);
    CHECK(!light_is_fading(&light));

    /* And it stays there. */
    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS + 10000u);
    CHECK_EQ_INT(light_current_brightness(&light), 200);
}

TEST(a_fade_never_goes_backwards_or_overshoots)
{
    light_t light;

    light_init(&light, 0);
    light_set_brightness(&light, 0, 0);
    settle(&light, WELL_PAST);

    light_set_brightness(&light, 255, WELL_PAST);

    uint8_t previous = 0;
    for (uint32_t at = 0; at <= LIGHT_FADE_BRIGHTNESS_MS + 100u; at += 10u) {
        settle(&light, WELL_PAST + at);
        const uint8_t now = light_current_brightness(&light);

        if (now < previous) {
            CHECK_EQ_INT(now, previous);   /* reports the offending pair */
            return;
        }
        previous = now;
    }
    CHECK_EQ_INT(previous, 255);
}

TEST(a_fade_eases_at_both_ends)
{
    /*
     * The point of smoothstep over a straight line. The first and last tenth
     * of the fade must each cover less ground than the middle tenth, or the
     * light visibly starts and stops with a corner.
     */
    light_t light;
    const uint32_t tenth = LIGHT_FADE_BRIGHTNESS_MS / 10u;

    light_init(&light, 0);
    light_set_brightness(&light, 0, 0);
    settle(&light, WELL_PAST);
    light_set_brightness(&light, 255, WELL_PAST);

    settle(&light, WELL_PAST + tenth);
    const int first = light_current_brightness(&light);

    settle(&light, WELL_PAST + 5u * tenth);
    const int before_middle = light_current_brightness(&light);
    settle(&light, WELL_PAST + 6u * tenth);
    const int after_middle = light_current_brightness(&light);
    const int middle = after_middle - before_middle;

    settle(&light, WELL_PAST + 9u * tenth);
    const int before_last = light_current_brightness(&light);
    settle(&light, WELL_PAST + 10u * tenth);
    const int last = light_current_brightness(&light) - before_last;

    CHECK(first < middle);
    CHECK(last < middle);
}

TEST(a_fade_interrupted_part_way_continues_from_where_it_actually_is)
{
    /* Dragging a brightness slider sends a stream of values; each new one has
       to start from the level on the strip, not from the last target. */
    light_t light;

    light_init(&light, 0);
    light_set_brightness(&light, 0, 0);
    settle(&light, WELL_PAST);

    light_set_brightness(&light, 255, WELL_PAST);
    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS / 2u);
    const uint8_t interrupted_at = light_current_brightness(&light);

    light_set_brightness(&light, 0, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS / 2u);

    /* No jump at the moment of the change. */
    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS / 2u);
    CHECK_EQ_INT(light_current_brightness(&light), interrupted_at);

    settle(&light, WELL_PAST + LIGHT_FADE_BRIGHTNESS_MS * 2u);
    CHECK_EQ_INT(light_current_brightness(&light), 0);
}

TEST(setting_the_brightness_already_showing_starts_no_fade)
{
    light_t light;

    light_init(&light, 0);
    light_set_brightness(&light, 100, 0);
    settle(&light, WELL_PAST);

    const uint32_t generation = light.generation;

    light_set_brightness(&light, 100, WELL_PAST);
    CHECK(!light_is_fading(&light));
    CHECK_EQ_INT((int)light.generation, (int)generation);
}

TEST(a_fade_survives_the_millisecond_counter_wrapping)
{
    /*
     * uint32_t milliseconds wrap a little under every 50 days. A fade started
     * just before the wrap must finish normally rather than jumping to its
     * target -- a bug that would only ever appear on a light left running for
     * seven weeks, which is exactly how long it would take to find on
     * hardware.
     */
    light_t light;
    const uint32_t before_wrap = 0xFFFFFFFFu - (LIGHT_FADE_BRIGHTNESS_MS / 2u);
    const uint32_t start = before_wrap - LIGHT_FADE_BRIGHTNESS_MS;

    /* Wind down to zero first, finishing exactly at `before_wrap`, so the
       fade under test is the one that straddles the wrap. */
    light_init(&light, start);
    light_set_brightness(&light, 0, start);
    settle(&light, before_wrap);
    CHECK_EQ_INT(light_current_brightness(&light), 0);

    light_set_brightness(&light, 240, before_wrap);

    /* A quarter of the way in, still short of the wrap. */
    settle(&light, before_wrap + LIGHT_FADE_BRIGHTNESS_MS / 4u);
    const uint8_t quarter = light_current_brightness(&light);
    CHECK(quarter > 0);
    CHECK(quarter < 240);

    /* Three quarters in, now past the wrap. */
    settle(&light, before_wrap + (3u * LIGHT_FADE_BRIGHTNESS_MS) / 4u);
    const uint8_t three_quarters = light_current_brightness(&light);
    CHECK(three_quarters > quarter);
    CHECK(three_quarters < 240);

    settle(&light, before_wrap + LIGHT_FADE_BRIGHTNESS_MS);
    CHECK_EQ_INT(light_current_brightness(&light), 240);
    CHECK(!light_is_fading(&light));
}

/* ---------------------------------------------------------------------------
 * Colour
 * -------------------------------------------------------------------------*/

TEST(setting_rgb_selects_rgb_mode_and_setting_mireds_selects_temperature)
{
    light_t light;

    light_init(&light, 0);

    light_set_color(&light, ws2812_rgb(10, 20, 30), 0);
    CHECK_EQ_INT((int)light.color_mode, (int)LIGHT_COLOR_MODE_RGB);

    const ws2812_color_t colour = light_current_color(&light);
    CHECK_EQ_INT(colour.r, 10);
    CHECK_EQ_INT(colour.g, 20);
    CHECK_EQ_INT(colour.b, 30);

    light_set_mireds(&light, 300, 0);
    CHECK_EQ_INT((int)light.color_mode, (int)LIGHT_COLOR_MODE_TEMP);

    /* Now the colour comes from the temperature, not the RGB still stored. */
    settle(&light, WELL_PAST);
    const ws2812_color_t warm = light_current_color(&light);
    CHECK(warm.r > warm.g);
    CHECK(warm.g > warm.b);

    /* Going back to RGB restores what was set, unchanged. */
    light_set_color(&light, ws2812_rgb(10, 20, 30), WELL_PAST);
    const ws2812_color_t again = light_current_color(&light);
    CHECK_EQ_INT(again.r, 10);
    CHECK_EQ_INT(again.b, 30);
}

TEST(the_colour_temperature_fade_moves_the_rendered_colour)
{
    light_t light;

    light_init(&light, 0);
    light_set_mireds(&light, LIGHT_MIREDS_MIN, 0);
    settle(&light, WELL_PAST);
    const ws2812_color_t cool = light_current_color(&light);

    light_set_mireds(&light, LIGHT_MIREDS_MAX, WELL_PAST);
    settle(&light, WELL_PAST + LIGHT_FADE_MIREDS_MS / 2u);
    const ws2812_color_t midway = light_current_color(&light);

    settle(&light, WELL_PAST + LIGHT_FADE_MIREDS_MS);
    const ws2812_color_t warm = light_current_color(&light);

    /* Blue falls away as the light warms, and the midpoint sits between. */
    CHECK(cool.b > midway.b);
    CHECK(midway.b > warm.b);
    CHECK_EQ_INT(light_current_mireds(&light), LIGHT_MIREDS_MAX);
}

TEST(mireds_outside_the_supported_range_are_clamped_not_refused)
{
    light_t light;

    light_init(&light, 0);

    light_set_mireds(&light, 1, 0);
    CHECK_EQ_INT((int)light.mireds, (int)LIGHT_MIREDS_MIN);

    light_set_mireds(&light, 60000, 0);
    CHECK_EQ_INT((int)light.mireds, (int)LIGHT_MIREDS_MAX);
}

TEST(the_colour_temperature_curve_is_exact_at_its_sample_points)
{
    /* Interpolation must not drift the samples themselves; these are the
       endpoints and one interior sample of the table. */
    const ws2812_color_t coolest = light_color_from_mireds(LIGHT_MIREDS_MIN);
    const ws2812_color_t warmest = light_color_from_mireds(500);
    const ws2812_color_t mid = light_color_from_mireds(300);

    CHECK_EQ_INT(coolest.r, 255);
    CHECK_EQ_INT(warmest.r, 255);
    CHECK_EQ_INT(warmest.g, 137);
    CHECK_EQ_INT(warmest.b, 14);
    CHECK_EQ_INT(mid.r, 255);
    CHECK_EQ_INT(mid.g, 188);
    CHECK_EQ_INT(mid.b, 131);
}

TEST(the_colour_temperature_curve_is_monotonic_across_the_whole_range)
{
    /*
     * Warmer means less green and less blue, at every step, with red pinned
     * at full. A table entry mistyped in the middle would break exactly this
     * and nothing else.
     */
    ws2812_color_t previous = light_color_from_mireds(LIGHT_MIREDS_MIN);

    for (uint16_t mireds = LIGHT_MIREDS_MIN + 1u; mireds <= LIGHT_MIREDS_MAX; mireds++) {
        const ws2812_color_t colour = light_color_from_mireds(mireds);

        if (colour.r != 255 || colour.g > previous.g || colour.b > previous.b) {
            CHECK_EQ_INT(colour.r, 255);
            CHECK(colour.g <= previous.g);
            CHECK(colour.b <= previous.b);
            return;
        }
        previous = colour;
    }

    /* The two ends must actually differ, or "monotonic" is satisfied by a
       table that never changes at all. */
    const ws2812_color_t coolest = light_color_from_mireds(LIGHT_MIREDS_MIN);
    CHECK(coolest.b > previous.b + 100);
}

TEST(the_colour_temperature_curve_clamps_outside_the_range)
{
    const ws2812_color_t below = light_color_from_mireds(0);
    const ws2812_color_t at_min = light_color_from_mireds(LIGHT_MIREDS_MIN);
    const ws2812_color_t above = light_color_from_mireds(65535);
    const ws2812_color_t at_max = light_color_from_mireds(LIGHT_MIREDS_MAX);

    CHECK_EQ_INT(below.b, at_min.b);
    CHECK_EQ_INT(above.b, at_max.b);
    CHECK_EQ_INT(above.g, at_max.g);
}

/* ---------------------------------------------------------------------------
 * Power and change tracking
 * -------------------------------------------------------------------------*/

TEST(power_is_independent_of_the_effect)
{
    /* Switching off and back on resumes what was running; a light that
       forgets its effect every time it is switched off is a nuisance. */
    light_t light;

    light_init(&light, 0);
    light_set_effect(&light, LIGHT_EFFECT_TWINKLE, 0);

    light_set_power(&light, false, 0);
    CHECK(!light.on);
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_TWINKLE);

    light_set_power(&light, true, 0);
    CHECK(light.on);
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_TWINKLE);
}

TEST(the_generation_counter_moves_only_on_a_real_change)
{
    /*
     * The application publishes a new state and rewrites flash when this
     * moves. A setter that bumped it unconditionally would republish on every
     * repeated command and wear the flash out over a slider drag.
     */
    light_t light;

    light_init(&light, 0);
    settle(&light, WELL_PAST);

    uint32_t generation = light.generation;

    light_set_power(&light, false, WELL_PAST);           /* already off */
    CHECK_EQ_INT((int)light.generation, (int)generation);

    light_set_power(&light, true, WELL_PAST);
    CHECK(light.generation > generation);
    generation = light.generation;

    light_set_effect(&light, light.effect, WELL_PAST);   /* already running */
    CHECK_EQ_INT((int)light.generation, (int)generation);

    light_set_effect(&light, LIGHT_EFFECT_WIPE, WELL_PAST);
    CHECK(light.generation > generation);
    generation = light.generation;

    light_set_color(&light, ws2812_rgb(1, 2, 3), WELL_PAST);
    CHECK(light.generation > generation);
    generation = light.generation;

    light_set_color(&light, ws2812_rgb(1, 2, 3), WELL_PAST);   /* same colour */
    CHECK_EQ_INT((int)light.generation, (int)generation);

    light_set_mireds(&light, 400, WELL_PAST);
    CHECK(light.generation > generation);
    generation = light.generation;

    light_set_mireds(&light, 400, WELL_PAST);            /* same temperature */
    CHECK_EQ_INT((int)light.generation, (int)generation);
}

TEST(returning_to_rgb_after_a_temperature_counts_as_a_change)
{
    /* The colour is the same triple, but the mode is not -- a controller
       showing "colour temperature" needs to be told it is RGB again. */
    light_t light;

    light_init(&light, 0);
    light_set_color(&light, ws2812_rgb(9, 9, 9), 0);
    const uint32_t generation = light.generation;

    light_set_mireds(&light, 300, 0);
    light_set_color(&light, ws2812_rgb(9, 9, 9), 0);

    CHECK(light.generation > generation + 1u);
    CHECK_EQ_INT((int)light.color_mode, (int)LIGHT_COLOR_MODE_RGB);
}

/* ---------------------------------------------------------------------------
 * Saving and restoring
 * -------------------------------------------------------------------------*/

TEST(settings_survive_a_capture_and_restore)
{
    light_t light;
    light_t restored;
    light_settings_t stored;

    light_init(&light, 0);
    light_set_color(&light, ws2812_rgb(3, 5, 7), 0);
    light_set_brightness(&light, 77, 0);
    light_set_effect(&light, LIGHT_EFFECT_TWINKLE, 0);
    light_set_power(&light, false, 0);
    settle(&light, WELL_PAST);

    light_capture(&light, &stored);
    light_restore(&restored, &stored, 0);

    CHECK(restored.on == light.on);
    CHECK_EQ_INT(restored.brightness, 77);
    CHECK_EQ_INT(restored.color.g, 5);
    CHECK_EQ_INT((int)restored.effect, (int)LIGHT_EFFECT_TWINKLE);
    CHECK_EQ_INT((int)restored.color_mode, (int)LIGHT_COLOR_MODE_RGB);
}

TEST(a_restored_light_is_already_where_it_belongs)
{
    /* Not fading up to it. A strip that ramps from black every time the power
       comes back is worse than one that just lights. */
    light_t light;
    const light_settings_t stored = {
        .on = true,
        .brightness = 210,
        .color = { 1, 2, 3, 0 },
        .mireds = 300,
        .color_mode = LIGHT_COLOR_MODE_TEMP,
        .effect = LIGHT_EFFECT_SOLID,
    };

    light_restore(&light, &stored, 5000);

    CHECK(!light_is_fading(&light));
    CHECK_EQ_INT(light_current_brightness(&light), 210);
    CHECK_EQ_INT(light_current_mireds(&light), 300);
}

TEST(nonsense_out_of_flash_falls_back_instead_of_being_believed)
{
    /*
     * These values reach the code from flash, which can hold whatever a
     * half-finished write or an older firmware left behind. An effect index
     * past the end of the table would be read straight out of the name array.
     */
    light_t light;
    light_settings_t stored = {
        .on = true,
        .brightness = 100,
        .color = { 1, 2, 3, 0 },
        .mireds = 60000,                       /* far outside the range */
        .color_mode = (light_color_mode_t)99,
        .effect = (light_effect_t)200,
    };

    light_restore(&light, &stored, 0);

    CHECK((unsigned)light.effect < (unsigned)LIGHT_EFFECT_COUNT);
    CHECK(light.mireds >= LIGHT_MIREDS_MIN);
    CHECK(light.mireds <= LIGHT_MIREDS_MAX);
    CHECK(light_effect_name(light.effect) != NULL);

    /* And the colour curve still gets a usable input. */
    const ws2812_color_t colour = light_current_color(&light);
    CHECK_EQ_INT(colour.r, 255);

    /* A completely absent stored block is the same as a fresh light. */
    light_restore(&light, NULL, 0);
    CHECK(!light.on);
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_SOLID);
    CHECK_EQ_INT(light.mireds, LIGHT_DEFAULT_MIREDS);
}

TEST(the_easing_curve_never_goes_backwards)
{
    /*
     * Checked directly and exhaustively, because both the fades and the
     * breathing effect ride on it, and the failure -- a single step
     * backwards, at a handful of inputs out of a thousand -- is invisible in
     * any test that only samples the curve.
     */
    uint16_t previous = light_ease(0);

    CHECK_EQ_INT(previous, 0);

    for (uint16_t t = 1; t <= (uint16_t)LIGHT_EASE_ONE; t++) {
        const uint16_t value = light_ease(t);

        if (value < previous) {
            CHECK_EQ_INT(value, previous);
            return;
        }
        previous = value;
    }

    CHECK_EQ_INT(previous, (int)LIGHT_EASE_ONE);
    CHECK_EQ_INT(light_ease(60000), (int)LIGHT_EASE_ONE);

    /* Symmetric about the midpoint, which is what makes the breathing wave
       rise and fall the same way. */
    CHECK_EQ_INT(light_ease(LIGHT_EASE_ONE / 2u), (int)LIGHT_EASE_ONE / 2);
}

TEST(a_null_light_is_survivable_everywhere)
{
    /* Every entry point is reachable from a callback; none of them may fault
       on a pointer that was never set up. */
    light_init(NULL, 0);
    light_set_power(NULL, true, 0);
    light_set_brightness(NULL, 10, 0);
    light_set_color(NULL, ws2812_rgb(1, 2, 3), 0);
    light_set_mireds(NULL, 200, 0);
    light_tick(NULL, 0);
    light_capture(NULL, NULL);
    light_restore(NULL, NULL, 0);

    CHECK(!light_set_effect(NULL, LIGHT_EFFECT_SOLID, 0));
    CHECK(!light_is_fading(NULL));
    CHECK_EQ_INT(light_current_brightness(NULL), 0);
    CHECK_EQ_INT(light_current_color(NULL).r, 0);
}

TEST_MAIN(
    RUN(effect_names_round_trip);
    RUN(the_effect_list_is_the_one_home_assistant_was_told_about);
    RUN(an_unknown_effect_name_is_refused);
    RUN(setting_an_out_of_range_effect_leaves_the_current_one_running);
    RUN(a_fresh_light_starts_off_at_solid_3000k);
    RUN(a_brightness_change_arrives_gradually_and_completely);
    RUN(a_fade_never_goes_backwards_or_overshoots);
    RUN(a_fade_eases_at_both_ends);
    RUN(a_fade_interrupted_part_way_continues_from_where_it_actually_is);
    RUN(setting_the_brightness_already_showing_starts_no_fade);
    RUN(a_fade_survives_the_millisecond_counter_wrapping);
    RUN(setting_rgb_selects_rgb_mode_and_setting_mireds_selects_temperature);
    RUN(the_colour_temperature_fade_moves_the_rendered_colour);
    RUN(mireds_outside_the_supported_range_are_clamped_not_refused);
    RUN(the_colour_temperature_curve_is_exact_at_its_sample_points);
    RUN(the_colour_temperature_curve_is_monotonic_across_the_whole_range);
    RUN(the_colour_temperature_curve_clamps_outside_the_range);
    RUN(power_is_independent_of_the_effect);
    RUN(the_generation_counter_moves_only_on_a_real_change);
    RUN(returning_to_rgb_after_a_temperature_counts_as_a_change);
    RUN(settings_survive_a_capture_and_restore);
    RUN(a_restored_light_is_already_where_it_belongs);
    RUN(nonsense_out_of_flash_falls_back_instead_of_being_believed);
    RUN(the_easing_curve_never_goes_backwards);
    RUN(a_null_light_is_survivable_everywhere);
)
