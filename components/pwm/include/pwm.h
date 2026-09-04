/*
 * pwm - one PWM output on a GPIO, set by frequency and duty cycle or by pulse
 * width.
 *
 * What DESIGN_DOC.md section 18 lists as a future component. It is a thin one
 * on purpose: the Pico SDK's `pwm_set_gpio_level()` and friends are already
 * clean, and section 2.1 says not to wrap an API that is. What the SDK does
 * not do is work out a divider and a wrap for a frequency you can name, or a
 * level for a pulse width in microseconds -- and that arithmetic, done by hand
 * in every application, is where the mistakes are. [`pwm_timing`](pwm_timing.h)
 * is that arithmetic, host-tested; this file is the twenty lines of register
 * writing around it.
 *
 * So: use this when you want to say "50 Hz, 1500 us". Use the SDK directly
 * when you want a phase-correct counter, a slice driven from a GPIO input, or
 * anything else in chapter 4.5 of the datasheet -- this component deliberately
 * does not reach for those.
 *
 * SLICES ARE SHARED, WHICH IS THE ONE THING TO KNOW
 *
 * Each PWM slice has two channels, A and B, on consecutive GPIOs -- 0 and 1,
 * 2 and 3, and so on. The two channels have **separate levels but one wrap and
 * one divider**, so they cannot run at different frequencies. Setting the
 * frequency of one silently changes the other, and on a robot that reads as a
 * servo twitching whenever an unrelated LED is dimmed.
 *
 * pwm_out_init() will not do that quietly: if the slice is already running at
 * a different frequency, it returns PWM_ERR_SLICE_BUSY rather than
 * reconfiguring somebody else's output. That check reads the hardware, not a
 * table this component keeps, so it works across a reboot into a running
 * peripheral and holds no global state. pwm_gpio_shares_slice() answers the
 * same question before any of it is configured.
 *
 * NAMING
 *
 * pwm.c includes the SDK's hardware/pwm.h, which owns pwm_init, pwm_config,
 * pwm_set_wrap, pwm_set_clkdiv, pwm_set_enabled, pwm_set_gpio_level and the
 * rest of the bare pwm_* namespace. This component's API is therefore prefixed
 * pwm_out_, uniformly. mqtt.h, tcp.h and udp.h made the same trade against
 * lwIP for the same reason.
 */

#ifndef PICO_FRAMEWORK_PWM_H
#define PICO_FRAMEWORK_PWM_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pwm.h"

#include "pwm_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PWM_OK = 0,
    PWM_ERR_INVALID_ARG,
    PWM_ERR_FREQUENCY_TOO_HIGH,  /* see pwm_timing_for_frequency() */
    PWM_ERR_FREQUENCY_TOO_LOW,
    PWM_ERR_SLICE_BUSY,          /* the other channel is at another frequency */
} pwm_result_t;

const char *pwm_result_name(pwm_result_t result);

typedef struct {
    /* The output pin. Its slice and channel follow from it; see the header
       comment on sharing. */
    uint gpio;

    /* What the slice will run at. Not always reachable exactly -- ask
       pwm_out_frequency() afterwards for what was achieved. */
    uint32_t frequency_hz;

    /* Where to start, in 1/65535ths of the period. 0 is off, which is what a
       motor or a servo wants before anything has told it otherwise. */
    uint16_t duty;

    /* Drive the pin low while the counter is below the level rather than
       high. For an active-low driver, or an LED wired to 3V3. */
    bool invert;

    /*
     * Start the slice counting immediately. False configures everything and
     * leaves the output idle, so several channels can be set up and then
     * started together with pwm_out_enable().
     */
    bool start_enabled;
} pwm_out_config_t;

typedef struct {
    pwm_out_config_t config;
    pwm_timing_t timing;

    /* Sampled at init. A caller that changes the system clock afterwards has
       changed every frequency here and must re-init; there is no callback
       from the SDK to notice it. */
    uint32_t clock_hz;

    uint16_t duty;
    uint slice;
    uint channel;
    bool enabled;
    bool initialised;
} pwm_out_t;

/*
 * Configure the pin, the slice and the channel, and set the initial duty.
 *
 * Returns PWM_ERR_SLICE_BUSY if the slice is already running at a different
 * frequency for its other channel -- see the header comment. Nothing is
 * changed in that case.
 */
pwm_result_t pwm_out_init(pwm_out_t *out, const pwm_out_config_t *config);

/*
 * Stop the output and return the pin to SIO, driven low.
 *
 * The slice is left alone if its other channel is still enabled: stopping the
 * counter would stop that one too.
 */
void pwm_out_deinit(pwm_out_t *out);

/* ---------------------------------------------------------------------------
 * Driving it
 * -------------------------------------------------------------------------*/

/* Duty cycle in 1/65535ths of the period. 0 is fully off, PWM_DUTY_MAX fully
   on, at any frequency. */
pwm_result_t pwm_out_set_duty(pwm_out_t *out, uint16_t duty);

/* Duty cycle as a percentage, for the many cases where that is what the
   caller actually has. 0..100; anything above 100 is refused rather than
   clamped, since it is a mistake rather than an intention. */
pwm_result_t pwm_out_set_duty_percent(pwm_out_t *out, uint8_t percent);

/*
 * High time in microseconds -- the form an RC servo or an ESC is specified
 * in, where what matters is the width of the pulse and not its ratio to a
 * frame nobody counts. A typical servo wants 50 Hz and 1000-2000 us.
 *
 * Clamped to the period rather than wrapped: asking for longer than the frame
 * gives a solid line, which is at least the failure the caller asked for.
 */
pwm_result_t pwm_out_set_pulse_us(pwm_out_t *out, uint32_t pulse_us);

/*
 * Change the frequency, keeping the current duty cycle.
 *
 * This rewrites the slice's wrap and divider, so it changes the other channel
 * on the same slice too. The duty cycle of *this* output is preserved because
 * it is held as a fraction and re-scaled here; the other channel's level is a
 * raw count and is not, so its duty cycle will shift. See the header comment.
 */
pwm_result_t pwm_out_set_frequency(pwm_out_t *out, uint32_t frequency_hz);

/* Start or stop the counter. Stopping leaves the pin at whatever level it was
   on, which for a motor is worth thinking about: set the duty to 0 first. */
void pwm_out_enable(pwm_out_t *out, bool enabled);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

/* What the slice actually runs at, which is not always what was asked for. */
uint32_t pwm_out_frequency(const pwm_out_t *out);

/* The period in microseconds, which is the number a pulse width is judged
   against. */
uint32_t pwm_out_period_us(const pwm_out_t *out);

/* Counts in one period. This is the duty and pulse-width resolution, and the
   number that tells you whether a frequency is too high to control finely. */
static inline uint32_t pwm_out_resolution(const pwm_out_t *out)
{
    return (uint32_t)out->timing.wrap + 1u;
}

static inline uint16_t pwm_out_duty(const pwm_out_t *out)
{
    return out->duty;
}

static inline bool pwm_out_is_enabled(const pwm_out_t *out)
{
    return out->enabled;
}

static inline uint pwm_out_slice(const pwm_out_t *out)
{
    return out->slice;
}

/* Would these two pins land on the same slice, and so be unable to run at
   different frequencies? Answerable before anything is configured. */
bool pwm_gpio_shares_slice(uint gpio_a, uint gpio_b);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_PWM_H */
