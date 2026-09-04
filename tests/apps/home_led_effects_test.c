/*
 * Host-side tests for the home_led effects.
 *
 * Rendering is the part of this application that is hardest to judge by
 * looking at it: "the wipe seems about right" is not a result. These check the
 * things that are actually decidable -- how many pixels are lit, that the
 * pattern moves, that nothing is written past the end of the buffer, that the
 * light really goes dark when it is switched off -- and leave whether it looks
 * good to a strip and an eye.
 */

#include "test.h"

#include "effects.h"
#include "light.h"

#define STRIP 32u
#define GUARD 8u

/* A buffer with a tail that nothing should ever touch, so a renderer that
   walks one past the end is caught here rather than by corrupting whatever
   the application put next to its pixel array. */
typedef struct {
    ws2812_color_t pixels[STRIP + GUARD];
} canvas_t;

static void canvas_init(canvas_t *canvas)
{
    for (size_t i = 0; i < STRIP + GUARD; i++) {
        canvas->pixels[i] = ws2812_rgb(0xAB, 0xCD, 0xEF);
    }
}

static bool guard_intact(const canvas_t *canvas)
{
    for (size_t i = STRIP; i < STRIP + GUARD; i++) {
        if (canvas->pixels[i].r != 0xAB || canvas->pixels[i].g != 0xCD ||
            canvas->pixels[i].b != 0xEF) {
            return false;
        }
    }
    return true;
}

static bool is_black(ws2812_color_t c)
{
    return c.r == 0 && c.g == 0 && c.b == 0;
}

static unsigned lit_count(const canvas_t *canvas)
{
    unsigned count = 0;

    for (unsigned i = 0; i < STRIP; i++) {
        if (!is_black(canvas->pixels[i])) {
            count++;
        }
    }
    return count;
}

/* A light settled on one effect, one brightness and one colour, with every
   fade already finished so the renderer sees stable inputs. */
static void prepare(light_t *light, light_effect_t effect, uint8_t brightness,
                    ws2812_color_t color)
{
    light_init(light, 0);
    light_set_power(light, true, 0);
    light_set_color(light, color, 0);
    light_set_brightness(light, brightness, 0);
    light_set_effect(light, effect, 0);
    light_tick(light, 100000u);
}

/* ---------------------------------------------------------------------------
 * Off
 * -------------------------------------------------------------------------*/

TEST(a_light_that_is_off_renders_black_not_its_last_frame)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_RAINBOW, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);
    CHECK(lit_count(&canvas) > 0);

    light_set_power(&light, false, 0);
    effects_render(&effects, &light, canvas.pixels, STRIP, 16);

    CHECK_EQ_INT((int)lit_count(&canvas), 0);
    CHECK(guard_intact(&canvas));
}

TEST(switching_back_on_resumes_the_effect)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 255, ws2812_rgb(10, 200, 30));

    light_set_power(&light, false, 0);
    effects_render(&effects, &light, canvas.pixels, STRIP, 0);
    CHECK_EQ_INT((int)lit_count(&canvas), 0);

    light_set_power(&light, true, 0);
    light_tick(&light, LIGHT_FADE_BRIGHTNESS_MS);
    effects_render(&effects, &light, canvas.pixels, STRIP, 16);

    CHECK_EQ_INT(canvas.pixels[0].g, 200);
}

/* ---------------------------------------------------------------------------
 * Solid
 * -------------------------------------------------------------------------*/

TEST(solid_paints_the_whole_strip_in_the_chosen_colour)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 255, ws2812_rgb(12, 34, 56));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    for (unsigned i = 0; i < STRIP; i++) {
        if (canvas.pixels[i].r != 12 || canvas.pixels[i].g != 34 ||
            canvas.pixels[i].b != 56) {
            CHECK_EQ_INT(canvas.pixels[i].r, 12);
            CHECK_EQ_INT(canvas.pixels[i].g, 34);
            CHECK_EQ_INT(canvas.pixels[i].b, 56);
            return;
        }
    }
    CHECK(guard_intact(&canvas));
}

TEST(solid_at_a_low_brightness_dithers_within_one_step)
{
    /*
     * Dithering deliberately makes neighbouring pixels differ; what it must
     * never do is differ by more than the one step it is trading for extra
     * depth, or the strip looks speckled rather than smooth.
     */
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 9, ws2812_rgb(200, 200, 200));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    uint8_t low = 255;
    uint8_t high = 0;
    for (unsigned i = 0; i < STRIP; i++) {
        const uint8_t value = canvas.pixels[i].r;

        low = value < low ? value : low;
        high = value > high ? value : high;
    }

    CHECK(high - low <= 1);
}

TEST(colour_temperature_mode_reaches_the_strip)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    light_init(&light, 0);
    light_set_power(&light, true, 0);
    light_set_brightness(&light, 255, 0);
    light_set_mireds(&light, LIGHT_MIREDS_MAX, 0);
    light_tick(&light, 100000u);

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    /* The warm end of the curve: strong red, much less blue. */
    CHECK_EQ_INT(canvas.pixels[0].r, 255);
    CHECK(canvas.pixels[0].b < 40);
}

/* ---------------------------------------------------------------------------
 * Rainbow
 * -------------------------------------------------------------------------*/

TEST(rainbow_spreads_colour_along_the_strip_and_turns_over_time)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_RAINBOW, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    /* Different pixels, different hues -- a rainbow that painted one colour
       would pass every other check here. */
    unsigned distinct = 0;
    for (unsigned i = 1; i < STRIP; i++) {
        if (canvas.pixels[i].r != canvas.pixels[0].r ||
            canvas.pixels[i].g != canvas.pixels[0].g ||
            canvas.pixels[i].b != canvas.pixels[0].b) {
            distinct++;
        }
    }
    CHECK(distinct > STRIP / 2u);

    const ws2812_color_t first_frame = canvas.pixels[0];

    /* And it moves. */
    for (unsigned frame = 0; frame < 10; frame++) {
        effects_render(&effects, &light, canvas.pixels, STRIP, 16u * (frame + 1u));
    }

    CHECK(canvas.pixels[0].r != first_frame.r ||
          canvas.pixels[0].g != first_frame.g ||
          canvas.pixels[0].b != first_frame.b);
    CHECK(guard_intact(&canvas));
}

TEST(rainbow_is_fully_saturated_at_full_brightness)
{
    /* Every pixel should sit on the wheel, i.e. one channel at full and one at
       nothing. A washed-out rainbow means the hue conversion is wrong. */
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_RAINBOW, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    for (unsigned i = 0; i < STRIP; i++) {
        const ws2812_color_t c = canvas.pixels[i];
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
 * Wipe
 * -------------------------------------------------------------------------*/

TEST(wipe_lights_a_short_trail_and_leaves_the_rest_dark)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_WIPE, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    const unsigned lit = lit_count(&canvas);
    CHECK(lit > 1);
    CHECK(lit < STRIP / 2u);
    CHECK(guard_intact(&canvas));
}

TEST(the_wipe_head_moves_and_wraps_round_the_end)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_WIPE, 255, ws2812_rgb(255, 255, 255));

    /* The brightest pixel is the head. */
    effects_render(&effects, &light, canvas.pixels, STRIP, 0);
    const unsigned first_head = 0;
    CHECK_EQ_INT(canvas.pixels[first_head].r, 255);

    effects_render(&effects, &light, canvas.pixels, STRIP, 16);
    CHECK_EQ_INT(canvas.pixels[1].r, 255);

    /* Run right round the strip and back to the start. */
    for (unsigned frame = 0; frame < STRIP - 2u; frame++) {
        effects_render(&effects, &light, canvas.pixels, STRIP, 32u + 16u * frame);
    }
    effects_render(&effects, &light, canvas.pixels, STRIP, 100000u);

    CHECK_EQ_INT(canvas.pixels[first_head].r, 255);
    CHECK(guard_intact(&canvas));
}

TEST(wipe_survives_a_strip_shorter_than_its_trail)
{
    /* The trail is six pixels; a three-pixel strip must not walk off it. */
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_WIPE, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, 3, 0);

    CHECK(!is_black(canvas.pixels[0]));
    /* Everything past the three requested pixels is untouched. */
    CHECK_EQ_INT(canvas.pixels[3].r, 0xAB);
    CHECK(guard_intact(&canvas));
}

/* ---------------------------------------------------------------------------
 * Twinkle
 * -------------------------------------------------------------------------*/

TEST(twinkle_sparks_a_minority_of_pixels_over_a_dim_background)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_TWINKLE, 255, ws2812_rgb(255, 255, 255));

    unsigned sparks = 0;
    unsigned frames = 0;

    for (frames = 0; frames < 20; frames++) {
        effects_render(&effects, &light, canvas.pixels, STRIP, 16u * frames);

        for (unsigned i = 0; i < STRIP; i++) {
            /* A spark is far brighter than the background, which is the base
               colour shifted down three bits. */
            if (canvas.pixels[i].r > 128) {
                sparks++;
            }
        }
    }

    /* Sparse, but not never: roughly a tenth of pixels, and the point is that
       it is neither zero nor everything. */
    CHECK(sparks > 0);
    CHECK(sparks < (STRIP * frames) / 2u);
    CHECK(guard_intact(&canvas));
}

TEST(twinkle_never_leaves_the_strip_completely_dark)
{
    /* The unlit pixels keep a dim version of the colour, so the strip reads as
       a lit thing with sparkles rather than as sparks in the void. */
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_TWINKLE, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);

    CHECK_EQ_INT((int)lit_count(&canvas), (int)STRIP);
}

TEST(twinkle_changes_from_frame_to_frame)
{
    canvas_t canvas;
    canvas_t before;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_TWINKLE, 255, ws2812_rgb(255, 255, 255));

    effects_render(&effects, &light, canvas.pixels, STRIP, 0);
    before = canvas;
    effects_render(&effects, &light, canvas.pixels, STRIP, 16);

    unsigned changed = 0;
    for (unsigned i = 0; i < STRIP; i++) {
        if (canvas.pixels[i].r != before.pixels[i].r) {
            changed++;
        }
    }
    CHECK(changed > 0);
}

/* ---------------------------------------------------------------------------
 * Breathing
 * -------------------------------------------------------------------------*/

TEST(breathing_swings_between_a_floor_and_full_over_its_period)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_BREATHING, 255, ws2812_rgb(255, 255, 255));

    /* First render fixes the effect's start time at now_ms. */
    effects_render(&effects, &light, canvas.pixels, STRIP, 0);
    const uint8_t trough = canvas.pixels[0].r;

    uint8_t lowest = 255;
    uint8_t highest = 0;
    for (uint32_t at = 0; at <= 8000u; at += 100u) {
        effects_render(&effects, &light, canvas.pixels, STRIP, at);

        const uint8_t value = canvas.pixels[0].r;
        lowest = value < lowest ? value : lowest;
        highest = value > highest ? value : highest;
    }

    CHECK_EQ_INT(highest, 255);

    /* It dims noticeably, but never goes out -- a light that reaches black
       reads as a fault rather than as breathing. */
    CHECK(lowest < 220);
    CHECK(lowest > 100);
    CHECK_EQ_INT(trough, lowest);
}

TEST(breathing_at_a_low_brightness_stays_low)
{
    /* The wave modulates the light's brightness; it must not override it, or
       a light dimmed for the evening flares to full once a second. */
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_BREATHING, 60, ws2812_rgb(255, 255, 255));

    uint8_t highest = 0;
    for (uint32_t at = 0; at <= 8000u; at += 100u) {
        effects_render(&effects, &light, canvas.pixels, STRIP, at);
        if (canvas.pixels[0].r > highest) {
            highest = canvas.pixels[0].r;
        }
    }

    CHECK(highest <= 61);
}

/* ---------------------------------------------------------------------------
 * Switching effects
 * -------------------------------------------------------------------------*/

TEST(selecting_a_new_effect_restarts_its_animation)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_WIPE, 255, ws2812_rgb(255, 255, 255));

    /* Run the wipe part way along the strip. */
    for (unsigned frame = 0; frame < 5; frame++) {
        effects_render(&effects, &light, canvas.pixels, STRIP, 16u * frame);
    }
    CHECK(is_black(canvas.pixels[0]) || canvas.pixels[0].r < 255);

    /* Leave and come back: the head is at the start again, not where it was. */
    light_set_effect(&light, LIGHT_EFFECT_SOLID, 0);
    effects_render(&effects, &light, canvas.pixels, STRIP, 200);
    light_set_effect(&light, LIGHT_EFFECT_WIPE, 0);
    effects_render(&effects, &light, canvas.pixels, STRIP, 216);

    CHECK_EQ_INT(canvas.pixels[0].r, 255);
}

/* ---------------------------------------------------------------------------
 * The test pattern, and robustness
 * -------------------------------------------------------------------------*/

TEST(the_test_pattern_marks_both_ends_of_the_strip)
{
    canvas_t canvas;

    canvas_init(&canvas);
    effects_render_test_pattern(canvas.pixels, STRIP);

    /* Red at the first pixel, green at the last, dim white between: which end
       is which and how many pixels the firmware thinks there are. */
    CHECK(canvas.pixels[0].r > canvas.pixels[0].g);
    CHECK(canvas.pixels[STRIP - 1u].g > canvas.pixels[STRIP - 1u].r);
    CHECK_EQ_INT(canvas.pixels[1].r, canvas.pixels[1].g);
    CHECK(guard_intact(&canvas));
}

TEST(the_test_pattern_survives_a_single_pixel_strip)
{
    canvas_t canvas;

    canvas_init(&canvas);
    effects_render_test_pattern(canvas.pixels, 1);

    /* One pixel is both the first and the last; the last write wins, and
       nothing is written before the buffer. */
    CHECK(!is_black(canvas.pixels[0]));
    CHECK_EQ_INT(canvas.pixels[1].r, 0xAB);
}

TEST(every_effect_stays_inside_the_buffer_it_was_given)
{
    /* One sweep over all of them, at an awkward length, watching the guard. */
    for (unsigned e = 0; e < (unsigned)LIGHT_EFFECT_COUNT; e++) {
        canvas_t canvas;
        effects_t effects;
        light_t light;

        canvas_init(&canvas);
        effects_init(&effects, 0);
        prepare(&light, (light_effect_t)e, 137, ws2812_rgb(200, 100, 50));

        for (unsigned frame = 0; frame < 40; frame++) {
            effects_render(&effects, &light, canvas.pixels, 7, 16u * frame);
        }

        if (!guard_intact(&canvas)) {
            CHECK(guard_intact(&canvas));
            return;
        }
        /* And nothing between the requested length and the guard either. */
        CHECK_EQ_INT(canvas.pixels[7].r, 0xAB);
    }
}

TEST(a_selected_range_lights_only_its_inclusive_endpoints)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;
    led_range_t range;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 255, ws2812_rgb(12, 34, 56));
    led_range_init(&range, STRIP);
    CHECK(led_range_set(&range, 5u, 12u));

    effects_render_range(&effects, &light, &range, canvas.pixels, STRIP, 0);

    for (unsigned i = 0; i < STRIP; i++) {
        if (i >= 4u && i < 12u) {
            CHECK_EQ_INT(canvas.pixels[i].g, 34);
        } else {
            CHECK(is_black(canvas.pixels[i]));
        }
    }
    CHECK(guard_intact(&canvas));
}

TEST(moving_a_range_clears_pixels_from_its_old_position)
{
    canvas_t canvas;
    effects_t effects;
    light_t light;
    led_range_t range;

    canvas_init(&canvas);
    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 255, ws2812_rgb(100, 100, 100));
    led_range_init(&range, STRIP);
    CHECK(led_range_set(&range, 2u, 8u));
    effects_render_range(&effects, &light, &range, canvas.pixels, STRIP, 0);
    CHECK(!is_black(canvas.pixels[1]));

    CHECK(led_range_set(&range, 20u, 24u));
    effects_render_range(&effects, &light, &range, canvas.pixels, STRIP, 16u);

    CHECK(is_black(canvas.pixels[1]));
    CHECK(!is_black(canvas.pixels[19]));
    CHECK(guard_intact(&canvas));
}

TEST(rendering_with_nothing_to_render_into_is_survivable)
{
    effects_t effects;
    light_t light;
    ws2812_color_t pixel = ws2812_rgb(1, 2, 3);

    effects_init(&effects, 0);
    prepare(&light, LIGHT_EFFECT_SOLID, 255, ws2812_rgb(9, 9, 9));

    effects_init(NULL, 0);
    effects_render(NULL, &light, &pixel, 1, 0);
    effects_render(&effects, NULL, &pixel, 1, 0);
    effects_render(&effects, &light, NULL, 1, 0);
    effects_render(&effects, &light, &pixel, 0, 0);
    effects_render_range(&effects, &light, NULL, &pixel, 1, 0);
    effects_render_range(&effects, &light, NULL, NULL, 1, 0);
    effects_render_range(&effects, &light, NULL, &pixel, 0, 0);
    effects_render_test_pattern(NULL, 4);
    effects_render_test_pattern(&pixel, 0);

    /* None of those may have written anything. */
    CHECK_EQ_INT(pixel.r, 1);
    CHECK_EQ_INT(pixel.g, 2);
    CHECK_EQ_INT(pixel.b, 3);
}

TEST_MAIN(
    RUN(a_light_that_is_off_renders_black_not_its_last_frame);
    RUN(switching_back_on_resumes_the_effect);
    RUN(solid_paints_the_whole_strip_in_the_chosen_colour);
    RUN(solid_at_a_low_brightness_dithers_within_one_step);
    RUN(colour_temperature_mode_reaches_the_strip);
    RUN(rainbow_spreads_colour_along_the_strip_and_turns_over_time);
    RUN(rainbow_is_fully_saturated_at_full_brightness);
    RUN(wipe_lights_a_short_trail_and_leaves_the_rest_dark);
    RUN(the_wipe_head_moves_and_wraps_round_the_end);
    RUN(wipe_survives_a_strip_shorter_than_its_trail);
    RUN(twinkle_sparks_a_minority_of_pixels_over_a_dim_background);
    RUN(twinkle_never_leaves_the_strip_completely_dark);
    RUN(twinkle_changes_from_frame_to_frame);
    RUN(breathing_swings_between_a_floor_and_full_over_its_period);
    RUN(breathing_at_a_low_brightness_stays_low);
    RUN(selecting_a_new_effect_restarts_its_animation);
    RUN(the_test_pattern_marks_both_ends_of_the_strip);
    RUN(the_test_pattern_survives_a_single_pixel_strip);
    RUN(every_effect_stays_inside_the_buffer_it_was_given);
    RUN(a_selected_range_lights_only_its_inclusive_endpoints);
    RUN(moving_a_range_clears_pixels_from_its_old_position);
    RUN(rendering_with_nothing_to_render_into_is_survivable);
)
