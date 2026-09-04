#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "pwm.h"

const char *pwm_result_name(pwm_result_t result)
{
    switch (result) {
        case PWM_OK:                     return "ok";
        case PWM_ERR_INVALID_ARG:        return "invalid argument";
        case PWM_ERR_FREQUENCY_TOO_HIGH: return "too fast for this clock";
        case PWM_ERR_FREQUENCY_TOO_LOW:  return "too slow for the divider";
        case PWM_ERR_SLICE_BUSY:         return "slice is running at another frequency";
        default:                         return "unknown";
    }
}

bool pwm_gpio_shares_slice(uint gpio_a, uint gpio_b)
{
    return pwm_gpio_to_slice_num(gpio_a) == pwm_gpio_to_slice_num(gpio_b);
}

static pwm_result_t result_for_timing(pwm_timing_result_t result)
{
    switch (result) {
        case PWM_TIMING_OK:        return PWM_OK;
        case PWM_TIMING_TOO_FAST:  return PWM_ERR_FREQUENCY_TOO_HIGH;
        case PWM_TIMING_TOO_SLOW:  return PWM_ERR_FREQUENCY_TOO_LOW;
        default:                   return PWM_ERR_INVALID_ARG;
    }
}

/* The DIV register's 8.4 layout: integer in bits 11:4, sixteenths in 3:0. */
static uint32_t div_register_value(const pwm_timing_t *timing)
{
    return ((uint32_t)timing->divider_int << 4) | (uint32_t)timing->divider_frac;
}

static bool slice_is_enabled(uint slice)
{
    return (pwm_hw->en & (1u << slice)) != 0u;
}

/*
 * Is this slice already running for somebody else, at settings this one would
 * have to overwrite?
 *
 * Read from the hardware rather than from a table of active outputs, which
 * means no global state and a correct answer even for a slice left running by
 * code that is no longer around -- a watchdog reboot with the motors still
 * turning, for instance.
 */
static bool slice_conflicts(uint slice, const pwm_timing_t *timing)
{
    if (!slice_is_enabled(slice)) {
        return false;
    }
    return pwm_hw->slice[slice].top != timing->wrap ||
           pwm_hw->slice[slice].div != div_register_value(timing);
}

static void apply_timing(pwm_out_t *out)
{
    pwm_set_clkdiv_int_frac(out->slice, out->timing.divider_int, out->timing.divider_frac);
    pwm_set_wrap(out->slice, out->timing.wrap);
}

/*
 * Set this channel's polarity without disturbing the other one's.
 *
 * pwm_set_output_polarity() writes both channels of the slice in one call, so
 * passing `false` for the channel this output does not own would clear an
 * inversion the other output had set -- the same class of accident as
 * overwriting its wrap, and just as quiet. The current bits are read back from
 * CSR and only this channel's is replaced.
 */
static void apply_polarity(pwm_out_t *out, bool invert)
{
    const uint32_t csr = pwm_hw->slice[out->slice].csr;
    bool invert_a = (csr & PWM_CH0_CSR_A_INV_BITS) != 0u;
    bool invert_b = (csr & PWM_CH0_CSR_B_INV_BITS) != 0u;

    if (out->channel == PWM_CHAN_A) {
        invert_a = invert;
    } else {
        invert_b = invert;
    }
    pwm_set_output_polarity(out->slice, invert_a, invert_b);
}

static void apply_duty(pwm_out_t *out)
{
    pwm_set_chan_level(out->slice, out->channel,
                       pwm_timing_level_for_duty(&out->timing, out->duty));
}

pwm_result_t pwm_out_init(pwm_out_t *out, const pwm_out_config_t *config)
{
    if (out == NULL || config == NULL) {
        return PWM_ERR_INVALID_ARG;
    }
    if (config->gpio >= NUM_BANK0_GPIOS) {
        return PWM_ERR_INVALID_ARG;
    }

    const uint32_t clock_hz = clock_get_hz(clk_sys);

    pwm_timing_t timing;
    const pwm_timing_result_t computed =
        pwm_timing_for_frequency(clock_hz, config->frequency_hz, &timing);
    if (computed != PWM_TIMING_OK) {
        return result_for_timing(computed);
    }

    const uint slice = pwm_gpio_to_slice_num(config->gpio);

    /* Refuse before touching anything: reconfiguring a slice somebody else is
       using would move their frequency without telling them. */
    if (slice_conflicts(slice, &timing)) {
        return PWM_ERR_SLICE_BUSY;
    }

    memset(out, 0, sizeof(*out));
    out->config = *config;
    out->timing = timing;
    out->clock_hz = clock_hz;
    out->duty = config->duty;
    out->slice = slice;
    out->channel = pwm_gpio_to_channel(config->gpio);
    out->initialised = true;

    apply_timing(out);
    apply_polarity(out, config->invert);
    apply_duty(out);

    /* The level is set before the pin is handed to the PWM block, so the
       output never carries a stale duty cycle for the moment in between --
       which on an ESC is a moment of throttle. */
    gpio_set_function(config->gpio, GPIO_FUNC_PWM);

    if (config->start_enabled) {
        pwm_set_enabled(out->slice, true);
        out->enabled = true;
    }
    return PWM_OK;
}

void pwm_out_deinit(pwm_out_t *out)
{
    if (out == NULL || !out->initialised) {
        return;
    }

    pwm_set_chan_level(out->slice, out->channel, 0);

    /*
     * Only stop the counter if nothing else needs it. The other channel of
     * this slice may still be driving something, and stopping the slice would
     * stop that too -- the sharing this component exists to be careful about.
     */
    const uint other_gpio = (out->config.gpio ^ 1u);
    const bool other_is_ours = pwm_gpio_to_slice_num(other_gpio) == out->slice &&
                               gpio_get_function(other_gpio) == GPIO_FUNC_PWM;
    if (!other_is_ours) {
        pwm_set_enabled(out->slice, false);
    }

    /* Back to SIO, driven low, so the pin is not left floating at whatever
       the last edge was. */
    gpio_set_function(out->config.gpio, GPIO_FUNC_SIO);
    gpio_set_dir(out->config.gpio, GPIO_OUT);
    gpio_put(out->config.gpio, false);

    out->enabled = false;
    out->initialised = false;
}

pwm_result_t pwm_out_set_duty(pwm_out_t *out, uint16_t duty)
{
    if (out == NULL || !out->initialised) {
        return PWM_ERR_INVALID_ARG;
    }
    out->duty = duty;
    apply_duty(out);
    return PWM_OK;
}

pwm_result_t pwm_out_set_duty_percent(pwm_out_t *out, uint8_t percent)
{
    if (out == NULL || !out->initialised || percent > 100u) {
        return PWM_ERR_INVALID_ARG;
    }
    /* Rounded so that 100 lands exactly on PWM_DUTY_MAX and 0 on 0. */
    return pwm_out_set_duty(out, (uint16_t)(((uint32_t)percent * PWM_DUTY_MAX) / 100u));
}

pwm_result_t pwm_out_set_pulse_us(pwm_out_t *out, uint32_t pulse_us)
{
    if (out == NULL || !out->initialised) {
        return PWM_ERR_INVALID_ARG;
    }

    const uint16_t level =
        pwm_timing_level_for_pulse_us(out->clock_hz, &out->timing, pulse_us);
    pwm_set_chan_level(out->slice, out->channel, level);

    /*
     * Keep `duty` honest: a later pwm_out_set_frequency() re-scales from it,
     * and a status command that printed the duty from before the pulse was
     * set would be showing something that is no longer on the pin.
     */
    const uint32_t period = pwm_out_resolution(out);
    out->duty = (uint16_t)(((uint32_t)level * PWM_DUTY_MAX + (period / 2u)) / period);
    return PWM_OK;
}

pwm_result_t pwm_out_set_frequency(pwm_out_t *out, uint32_t frequency_hz)
{
    if (out == NULL || !out->initialised) {
        return PWM_ERR_INVALID_ARG;
    }

    pwm_timing_t timing;
    const pwm_timing_result_t computed =
        pwm_timing_for_frequency(out->clock_hz, frequency_hz, &timing);
    if (computed != PWM_TIMING_OK) {
        return result_for_timing(computed);
    }

    out->timing = timing;
    out->config.frequency_hz = frequency_hz;
    apply_timing(out);

    /* Held as a fraction, so it survives the wrap changing underneath it. */
    apply_duty(out);
    return PWM_OK;
}

void pwm_out_enable(pwm_out_t *out, bool enabled)
{
    if (out == NULL || !out->initialised) {
        return;
    }
    pwm_set_enabled(out->slice, enabled);
    out->enabled = enabled;
}

uint32_t pwm_out_frequency(const pwm_out_t *out)
{
    if (out == NULL || !out->initialised) {
        return 0u;
    }
    return pwm_timing_frequency(out->clock_hz, &out->timing);
}

uint32_t pwm_out_period_us(const pwm_out_t *out)
{
    if (out == NULL || !out->initialised) {
        return 0u;
    }
    return pwm_timing_period_us(out->clock_hz, &out->timing);
}
