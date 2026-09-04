/*
 * Host-side tests for PWM divider, wrap and level arithmetic.
 *
 * This is the half of the component that decides what the hardware does, and
 * the only other way to check it is an oscilloscope. A frequency that is 2%
 * off does not fail to build and does not look wrong in a debugger; it shows
 * up as a servo that buzzes, or an ESC that will not arm.
 *
 * The reference throughout is a 125 MHz system clock, which is the RP2040's
 * default and the number every worked example below can be checked against by
 * hand.
 */

#include "test.h"

#include "pwm_timing.h"

#define CLK_125 125000000u
#define CLK_150 150000000u

static pwm_timing_t timing_for(uint32_t clock_hz, uint32_t target_hz)
{
    pwm_timing_t timing;
    const pwm_timing_result_t result = pwm_timing_for_frequency(clock_hz, target_hz, &timing);
    CHECK_EQ_INT(result, PWM_TIMING_OK);
    return timing;
}

/* ---------------------------------------------------------------------------
 * Choosing a divider and a wrap
 * -------------------------------------------------------------------------*/

TEST(a_servo_frame_is_50hz_and_20ms)
{
    /*
     * The case the component exists for. 125e6 * 16 / 50 = 40,000,000
     * sixteenths per frame, which factors into a divider of 611/16 = 38.1875
     * and 65,466 counts.
     */
    const pwm_timing_t timing = timing_for(CLK_125, 50);

    CHECK_EQ_U32(pwm_timing_div16(&timing), 611);
    CHECK_EQ_INT(timing.divider_int, 38);
    CHECK_EQ_INT(timing.divider_frac, 3);      /* 38 + 3/16 = 38.1875 */
    CHECK_EQ_U32(timing.wrap, 65465);

    CHECK_EQ_U32(pwm_timing_frequency(CLK_125, &timing), 50);
    CHECK_EQ_U32(pwm_timing_period_us(CLK_125, &timing), 20000);
}

TEST(the_same_frame_on_a_150mhz_rp2350)
{
    /* A different clock must still land on 50 Hz and 20 ms, with different
       register values -- which is the whole reason the clock is a parameter
       rather than a constant. */
    const pwm_timing_t timing = timing_for(CLK_150, 50);

    CHECK_EQ_U32(pwm_timing_div16(&timing), 733);
    CHECK_EQ_U32(timing.wrap, 65483);
    CHECK_EQ_U32(pwm_timing_frequency(CLK_150, &timing), 50);
    CHECK_EQ_U32(pwm_timing_period_us(CLK_150, &timing), 20000);
}

TEST(frequencies_that_divide_the_clock_are_exact)
{
    const uint32_t targets[] = { 1000u, 25000u, 100000u, 1000000u };

    for (unsigned i = 0; i < count_of_(targets); i++) {
        const pwm_timing_t timing = timing_for(CLK_125, targets[i]);
        CHECK_EQ_U32(pwm_timing_frequency(CLK_125, &timing), targets[i]);
    }
}

TEST(the_divider_is_kept_as_small_as_the_wrap_allows)
{
    /*
     * Resolution is counts per period, and the divider is what throws them
     * away. Anything that fits inside a 16-bit wrap at divider 1.0 must use
     * divider 1.0 -- a "correct" frequency reached with a divider of 8 would
     * have an eighth of the duty resolution and nothing would say so.
     */
    const pwm_timing_t fast = timing_for(CLK_125, 25000);
    CHECK_EQ_U32(pwm_timing_div16(&fast), PWM_TIMING_MIN_DIV16);
    CHECK_EQ_U32(fast.wrap, 4999);           /* 5000 counts */

    /* And where it does not fit, the divider is the smallest that makes it,
       so the period stays as close to the 16-bit ceiling as possible. */
    const pwm_timing_t slow = timing_for(CLK_125, 50);
    CHECK(slow.wrap > 65000);
}

TEST(the_reachable_range_is_bounded_at_both_ends)
{
    pwm_timing_t timing;

    /* Faster than one count per period is not a PWM signal. */
    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 200000000u, &timing),
                 PWM_TIMING_TOO_FAST);

    /* The slow end is the 8.4 divider running out at 255.9375. At 125 MHz
       that lands between 7 and 8 Hz. */
    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 8u, &timing), PWM_TIMING_OK);
    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 7u, &timing), PWM_TIMING_TOO_SLOW);
    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 1u, &timing), PWM_TIMING_TOO_SLOW);
}

TEST(nothing_is_computed_from_nonsense)
{
    pwm_timing_t timing;

    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 1000u, NULL), PWM_TIMING_INVALID_ARG);
    CHECK_EQ_INT(pwm_timing_for_frequency(0u, 1000u, &timing), PWM_TIMING_INVALID_ARG);
    CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, 0u, &timing), PWM_TIMING_INVALID_ARG);

    CHECK_EQ_U32(pwm_timing_frequency(CLK_125, NULL), 0);
    CHECK_EQ_U32(pwm_timing_period_us(CLK_125, NULL), 0);
    CHECK_EQ_U32(pwm_timing_level_for_duty(NULL, 1000), 0);
    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, NULL, 1500), 0);
}

TEST(every_reachable_frequency_comes_back_within_a_fifth_of_a_percent)
{
    /*
     * A sweep rather than a handful of anchors: the rounding in
     * pwm_timing_for_frequency() has several places to go wrong, and a case
     * that is only slightly off is exactly the one nobody notices.
     */
    for (uint32_t target = 10u; target <= 100000u; target = (target * 3u) / 2u) {
        pwm_timing_t timing;
        CHECK_EQ_INT(pwm_timing_for_frequency(CLK_125, target, &timing), PWM_TIMING_OK);

        const uint32_t achieved = pwm_timing_frequency(CLK_125, &timing);
        const uint32_t error = (achieved > target) ? (achieved - target) : (target - achieved);

        /* error/target <= 0.2%, without floating point. */
        if (error * 500u > target) {
            TEST_FAIL_("target %u Hz: achieved %u Hz", (unsigned)target, (unsigned)achieved);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Duty cycle
 * -------------------------------------------------------------------------*/

TEST(duty_ends_are_fully_off_and_fully_on)
{
    const pwm_timing_t timing = timing_for(CLK_125, 1000);
    const uint32_t period = (uint32_t)timing.wrap + 1u;

    /* 0 must be a flat line, not one count of glitch per period. */
    CHECK_EQ_U32(pwm_timing_level_for_duty(&timing, 0), 0);

    /* And full duty must be a level *above* the wrap, which is what the
       hardware reads as always high. */
    CHECK_EQ_U32(pwm_timing_level_for_duty(&timing, PWM_DUTY_MAX), period);
    CHECK(pwm_timing_level_for_duty(&timing, PWM_DUTY_MAX) > timing.wrap);
}

TEST(duty_scales_across_the_period)
{
    const pwm_timing_t timing = timing_for(CLK_125, 1000);
    const uint32_t period = (uint32_t)timing.wrap + 1u;

    const uint16_t half = pwm_timing_level_for_duty(&timing, PWM_DUTY_MAX / 2u);
    const uint32_t expected_half = period / 2u;
    CHECK(half >= expected_half - 1u && half <= expected_half + 1u);

    /* Monotonic, with no step backwards anywhere -- a fade that goes briefly
       darker as it brightens is the visible symptom of bad rounding here. */
    uint16_t previous = 0;
    for (uint32_t duty = 0; duty <= PWM_DUTY_MAX; duty += 257u) {
        const uint16_t level = pwm_timing_level_for_duty(&timing, (uint16_t)duty);
        CHECK(level >= previous);
        previous = level;
    }
}

TEST(a_full_duty_level_still_fits_a_uint16_at_the_longest_period)
{
    /*
     * The reason PWM_TIMING_MAX_PERIOD_COUNTS is 65535 and not 65536: the
     * level register is 16 bits, and "always high" needs a level one above
     * the wrap. One count short of the hardware maximum is what makes that
     * value expressible at every frequency.
     */
    pwm_timing_t timing;
    timing.divider_int = 255;
    timing.divider_frac = 15;
    timing.wrap = (uint16_t)(PWM_TIMING_MAX_PERIOD_COUNTS - 1u);

    const uint32_t level = pwm_timing_level_for_duty(&timing, PWM_DUTY_MAX);
    CHECK_EQ_U32(level, PWM_TIMING_MAX_PERIOD_COUNTS);
    CHECK(level <= 65535u);

    /* Every frequency the component will choose obeys that cap. */
    for (uint32_t target = 8u; target <= 200u; target++) {
        pwm_timing_t chosen;
        if (pwm_timing_for_frequency(CLK_125, target, &chosen) != PWM_TIMING_OK) {
            continue;
        }
        CHECK((uint32_t)chosen.wrap + 1u <= PWM_TIMING_MAX_PERIOD_COUNTS);
    }
}

/* ---------------------------------------------------------------------------
 * Pulse width
 * -------------------------------------------------------------------------*/

TEST(servo_pulse_widths_land_where_the_datasheets_say)
{
    /* 50 Hz, so a 20 ms frame of 65466 counts: 1.5 ms is 7.5% of it. */
    const pwm_timing_t timing = timing_for(CLK_125, 50);

    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 1000), 3273);
    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 1500), 4910);
    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 2000), 6547);

    /* Which is exactly the 7.5% of a 20 ms frame that 1.5 ms should be. */
    const uint32_t period = (uint32_t)timing.wrap + 1u;
    CHECK_EQ_U32((4910u * 1000u) / period, 75);   /* per mille */
}

TEST(a_pulse_of_nothing_is_nothing_and_an_overlong_one_is_a_solid_line)
{
    const pwm_timing_t timing = timing_for(CLK_125, 50);
    const uint32_t period = (uint32_t)timing.wrap + 1u;

    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 0), 0);

    /* Longer than the 20 ms frame. Clamped, because wrapping round would turn
       a request for "always on" into a very short pulse -- which on an ESC is
       the difference between full throttle and none. */
    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 20000), period);
    CHECK_EQ_U32(pwm_timing_level_for_pulse_us(CLK_125, &timing, 99999), period);
}

TEST(pulse_widths_agree_with_the_period_they_are_measured_against)
{
    /* Half the frame asked for as a pulse and as a duty cycle must be the
       same level, whatever the frequency. */
    const uint32_t targets[] = { 50u, 200u, 1000u };

    for (unsigned i = 0; i < count_of_(targets); i++) {
        const pwm_timing_t timing = timing_for(CLK_125, targets[i]);
        const uint32_t period_us = pwm_timing_period_us(CLK_125, &timing);

        const uint16_t by_pulse =
            pwm_timing_level_for_pulse_us(CLK_125, &timing, period_us / 2u);
        const uint16_t by_duty = pwm_timing_level_for_duty(&timing, PWM_DUTY_MAX / 2u);

        const uint32_t difference = (by_pulse > by_duty) ? (uint32_t)(by_pulse - by_duty)
                                                         : (uint32_t)(by_duty - by_pulse);
        /* Within a count or two: the two come from different roundings. */
        CHECK(difference <= 2u);
    }
}

TEST(every_result_has_a_name)
{
    CHECK_EQ_STR(pwm_timing_result_name(PWM_TIMING_OK), "ok");
    CHECK_EQ_STR(pwm_timing_result_name(PWM_TIMING_INVALID_ARG), "invalid argument");
    CHECK_EQ_STR(pwm_timing_result_name(PWM_TIMING_TOO_FAST), "too fast for this clock");
    CHECK_EQ_STR(pwm_timing_result_name(PWM_TIMING_TOO_SLOW), "too slow for the divider");
    CHECK_EQ_STR(pwm_timing_result_name((pwm_timing_result_t)99), "unknown");
}

TEST_MAIN(
    RUN(a_servo_frame_is_50hz_and_20ms);
    RUN(the_same_frame_on_a_150mhz_rp2350);
    RUN(frequencies_that_divide_the_clock_are_exact);
    RUN(the_divider_is_kept_as_small_as_the_wrap_allows);
    RUN(the_reachable_range_is_bounded_at_both_ends);
    RUN(nothing_is_computed_from_nonsense);
    RUN(every_reachable_frequency_comes_back_within_a_fifth_of_a_percent);
    RUN(duty_ends_are_fully_off_and_fully_on);
    RUN(duty_scales_across_the_period);
    RUN(a_full_duty_level_still_fits_a_uint16_at_the_longest_period);
    RUN(servo_pulse_widths_land_where_the_datasheets_say);
    RUN(a_pulse_of_nothing_is_nothing_and_an_overlong_one_is_a_solid_line);
    RUN(pulse_widths_agree_with_the_period_they_are_measured_against);
    RUN(every_result_has_a_name);
)
