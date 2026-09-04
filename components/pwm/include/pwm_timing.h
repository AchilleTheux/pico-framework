/*
 * pwm_timing - divider and wrap values for a target PWM frequency, and the
 * level values for a duty cycle or a pulse width.
 *
 * This is the arithmetic the Pico SDK leaves to the caller. `pwm_set_wrap()`
 * and `pwm_set_clkdiv()` take the answers; nothing in the SDK works out what
 * they should be for "I want 50 Hz" or "I want a 1500 us pulse", and doing it
 * by hand in every application is how a servo ends up at 49.2 Hz because a
 * divider was rounded the convenient way.
 *
 * SDK-independent and integer-only, so it is host-tested rather than checked
 * by watching an oscilloscope. There are no floats anywhere here: the RP2040
 * has no hardware for them, and a frequency computed in software float at
 * startup is slower and no more accurate than this.
 *
 * THE HARDWARE THIS DESCRIBES
 *
 * A PWM slice counts from 0 to TOP and wraps, so one period is TOP+1 counts.
 * The counter is fed by the system clock through an 8.4 fixed-point divider:
 * an integer part of 1..255 and a fraction in sixteenths. So
 *
 *     tick_rate = clock_hz * 16 / div16          (div16 = int * 16 + frac)
 *     frequency = tick_rate / (TOP + 1)
 *
 * and the output is high while the counter is below the channel's level.
 * A level of 0 is therefore always low, and a level above TOP is always high.
 */

#ifndef PICO_FRAMEWORK_PWM_TIMING_H
#define PICO_FRAMEWORK_PWM_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Duty cycle is a fraction of the period in 1/65535ths, so that a caller need
 * not know the wrap value that happened to be chosen for its frequency. 0 is
 * fully off and PWM_DUTY_MAX fully on, at every frequency.
 */
#define PWM_DUTY_MAX 65535u

/* The divider's range, in sixteenths: 1.0 to 255.9375. */
#define PWM_TIMING_MIN_DIV16 16u
#define PWM_TIMING_MAX_DIV16 4095u

/*
 * Longest period the counter can express, in counts.
 *
 * The hardware's TOP is a full uint16, so 65536 counts are reachable -- but a
 * level is also a uint16, and "always high" needs a level above TOP. At
 * TOP = 65535 there is no such value, and full duty would come out as
 * 65535/65536 instead of a solid line. One count of resolution is a cheap
 * price for `PWM_DUTY_MAX` meaning what it says at every frequency, so the
 * period is capped one short.
 */
#define PWM_TIMING_MAX_PERIOD_COUNTS 65535u

typedef struct {
    uint8_t divider_int;    /* 1..255 */
    uint8_t divider_frac;   /* 0..15, sixteenths */
    uint16_t wrap;          /* TOP; the period is wrap + 1 counts */
} pwm_timing_t;

typedef enum {
    PWM_TIMING_OK = 0,
    PWM_TIMING_INVALID_ARG,
    PWM_TIMING_TOO_FAST,   /* fewer than one count per period at this clock */
    PWM_TIMING_TOO_SLOW,   /* past what the divider and a 16-bit wrap reach */
} pwm_timing_result_t;

const char *pwm_timing_result_name(pwm_timing_result_t result);

/*
 * Work out the divider and wrap that come closest to `target_hz`.
 *
 * The divider is made as small as the wrap allows, because every halving of
 * the divider doubles the counts in a period and so doubles the duty and
 * pulse-width resolution. At 125 MHz that gives 65466 counts for a 50 Hz
 * servo frame -- about 0.3 us of pulse resolution.
 *
 * The frequency is not always exact: only some targets divide the system
 * clock evenly. Ask pwm_timing_frequency() what was actually achieved rather
 * than assuming, and see the README for where the error becomes visible
 * (which is at high frequencies, where a period is few enough counts that
 * rounding one of them matters).
 */
pwm_timing_result_t pwm_timing_for_frequency(uint32_t clock_hz, uint32_t target_hz,
                                             pwm_timing_t *timing);

/* The frequency these values actually produce, rounded to the nearest hertz. */
uint32_t pwm_timing_frequency(uint32_t clock_hz, const pwm_timing_t *timing);

/* The period these values actually produce, rounded to the nearest
   microsecond. 0 if that would round to nothing. */
uint32_t pwm_timing_period_us(uint32_t clock_hz, const pwm_timing_t *timing);

/*
 * The channel level for a duty cycle in 1/65535ths.
 *
 * 0 gives 0 (always low) and PWM_DUTY_MAX gives wrap + 1 (always high), with
 * everything between scaled and rounded to nearest.
 */
uint16_t pwm_timing_level_for_duty(const pwm_timing_t *timing, uint16_t duty);

/*
 * The channel level for a pulse of `pulse_us` microseconds -- the form an RC
 * servo or an ESC is specified in, where what matters is the width of the
 * high time and not its ratio to a frame nobody counts.
 *
 * Clamped to the period: asking for a pulse longer than the frame gives a
 * solid line rather than wrapping round to a short one.
 */
uint16_t pwm_timing_level_for_pulse_us(uint32_t clock_hz, const pwm_timing_t *timing,
                                       uint32_t pulse_us);

/* The divider as sixteenths, which is the form all of the above works in. */
static inline uint32_t pwm_timing_div16(const pwm_timing_t *timing)
{
    return ((uint32_t)timing->divider_int * 16u) + timing->divider_frac;
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_PWM_TIMING_H */
