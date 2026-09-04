#include <stddef.h>

#include "pwm_timing.h"

const char *pwm_timing_result_name(pwm_timing_result_t result)
{
    switch (result) {
        case PWM_TIMING_OK:          return "ok";
        case PWM_TIMING_INVALID_ARG: return "invalid argument";
        case PWM_TIMING_TOO_FAST:    return "too fast for this clock";
        case PWM_TIMING_TOO_SLOW:    return "too slow for the divider";
        default:                     return "unknown";
    }
}

/* Round a/b to nearest, for positive integers. */
static uint64_t div_round(uint64_t a, uint64_t b)
{
    return (a + (b / 2u)) / b;
}

pwm_timing_result_t pwm_timing_for_frequency(uint32_t clock_hz, uint32_t target_hz,
                                             pwm_timing_t *timing)
{
    if (timing == NULL || clock_hz == 0u || target_hz == 0u) {
        return PWM_TIMING_INVALID_ARG;
    }

    /*
     * One period is div16 * (wrap + 1) sixteenths of a clock tick, so the
     * whole problem is factoring
     *
     *     total = clock_hz * 16 / target_hz
     *
     * into a divider within 16..4095 and a period within 1..65535 counts.
     */
    const uint64_t total = div_round((uint64_t)clock_hz * 16u, target_hz);

    if (total < PWM_TIMING_MIN_DIV16) {
        /* Not even one count per period at the fastest the counter runs. */
        return PWM_TIMING_TOO_FAST;
    }

    /*
     * The smallest divider that still leaves the period inside a 16-bit wrap.
     * Smallest, because counts are resolution: halving the divider doubles
     * how finely a duty cycle or a pulse width can be placed.
     */
    uint64_t div16 = (total + PWM_TIMING_MAX_PERIOD_COUNTS - 1u) / PWM_TIMING_MAX_PERIOD_COUNTS;
    if (div16 < PWM_TIMING_MIN_DIV16) {
        div16 = PWM_TIMING_MIN_DIV16;
    }
    if (div16 > PWM_TIMING_MAX_DIV16) {
        return PWM_TIMING_TOO_SLOW;
    }

    uint64_t period = div_round(total, div16);
    if (period < 1u) {
        period = 1u;
    }
    if (period > PWM_TIMING_MAX_PERIOD_COUNTS) {
        period = PWM_TIMING_MAX_PERIOD_COUNTS;
    }

    timing->divider_int = (uint8_t)(div16 / 16u);
    timing->divider_frac = (uint8_t)(div16 % 16u);
    timing->wrap = (uint16_t)(period - 1u);
    return PWM_TIMING_OK;
}

uint32_t pwm_timing_frequency(uint32_t clock_hz, const pwm_timing_t *timing)
{
    if (timing == NULL) {
        return 0u;
    }
    const uint64_t div16 = pwm_timing_div16(timing);
    if (div16 == 0u) {
        return 0u;
    }
    const uint64_t period = (uint64_t)timing->wrap + 1u;
    return (uint32_t)div_round((uint64_t)clock_hz * 16u, div16 * period);
}

uint32_t pwm_timing_period_us(uint32_t clock_hz, const pwm_timing_t *timing)
{
    if (timing == NULL || clock_hz == 0u) {
        return 0u;
    }
    const uint64_t div16 = pwm_timing_div16(timing);
    const uint64_t period = (uint64_t)timing->wrap + 1u;

    /* period counts * div16 / 16 clock ticks, at clock_hz ticks per second. */
    return (uint32_t)div_round(period * div16 * 1000000u, (uint64_t)clock_hz * 16u);
}

uint16_t pwm_timing_level_for_duty(const pwm_timing_t *timing, uint16_t duty)
{
    if (timing == NULL || duty == 0u) {
        return 0u;
    }

    const uint32_t period = (uint32_t)timing->wrap + 1u;

    /*
     * Scaled so that PWM_DUTY_MAX lands exactly on `period`, which is one
     * above the wrap and so a solid line. The period is capped at
     * PWM_TIMING_MAX_PERIOD_COUNTS precisely so that value still fits a
     * uint16 -- see PWM_TIMING_MAX_PERIOD_COUNTS.
     */
    return (uint16_t)(((uint32_t)duty * period + (PWM_DUTY_MAX / 2u)) / PWM_DUTY_MAX);
}

uint16_t pwm_timing_level_for_pulse_us(uint32_t clock_hz, const pwm_timing_t *timing,
                                       uint32_t pulse_us)
{
    if (timing == NULL || clock_hz == 0u || pulse_us == 0u) {
        return 0u;
    }

    const uint64_t div16 = pwm_timing_div16(timing);
    if (div16 == 0u) {
        return 0u;
    }

    /* counts = pulse_us * tick_rate / 1e6, with tick_rate = clock_hz*16/div16.
       In 64 bits because a 20 ms frame at 150 MHz overflows 32 partway. */
    const uint64_t counts = div_round((uint64_t)pulse_us * (uint64_t)clock_hz * 16u,
                                      div16 * 1000000u);

    const uint32_t period = (uint32_t)timing->wrap + 1u;
    if (counts >= period) {
        /* A pulse at least as long as the frame is a solid line, not a short
           one: clamping beats wrapping round. */
        return (uint16_t)period;
    }
    return (uint16_t)counts;
}
