/*
 * pwm_test - bench for the pwm component.
 *
 * An interactive command line onto one PWM output, plus the two things that
 * need a second one: proving that a paired GPIO shares the slice, and that
 * asking it for a different frequency is refused rather than silently moving
 * the first output.
 *
 * See README.md.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "pwm.h"

#ifndef PWM_TEST_GPIO
#define PWM_TEST_GPIO 10u
#endif

/* The other channel of the same slice, which is what the sharing tests use. */
#ifndef PWM_TEST_SECOND_GPIO
#define PWM_TEST_SECOND_GPIO 11u
#endif

#ifndef PWM_TEST_FREQUENCY_HZ
#define PWM_TEST_FREQUENCY_HZ 1000u
#endif

static pwm_out_t output;
static pwm_out_t second;
static bool second_initialised;

static char line_buffer[128];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 16u];

static int cmd_freq(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "asked %lu Hz, running %lu Hz\r\n",
                   (unsigned long)output.config.frequency_hz,
                   (unsigned long)pwm_out_frequency(&output));
        return CLI_OK;
    }

    const pwm_result_t result = pwm_out_set_frequency(&output, value);
    if (result != PWM_OK) {
        cli_printf(c, "refused: %s\r\n", pwm_result_name(result));
        return CLI_ERR_RANGE;
    }
    cli_printf(c, "%lu Hz, %lu counts per period\r\n",
               (unsigned long)pwm_out_frequency(&output),
               (unsigned long)pwm_out_resolution(&output));
    return CLI_OK;
}

static int cmd_duty(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "duty %u of %u\r\n", (unsigned)pwm_out_duty(&output),
                   (unsigned)PWM_DUTY_MAX);
        return CLI_OK;
    }
    if (value > PWM_DUTY_MAX) {
        cli_printf(c, "refused: 0..%u\r\n", (unsigned)PWM_DUTY_MAX);
        return CLI_ERR_RANGE;
    }
    pwm_out_set_duty(&output, (uint16_t)value);
    return CLI_OK;
}

static int cmd_pct(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_write(c, "usage: pct <0-100>\r\n");
        return CLI_ERR_ARG;
    }
    const pwm_result_t result = pwm_out_set_duty_percent(&output, (uint8_t)value);
    if (result != PWM_OK) {
        cli_printf(c, "refused: %s\r\n", pwm_result_name(result));
        return CLI_ERR_RANGE;
    }
    cli_printf(c, "%lu%%\r\n", (unsigned long)value);
    return CLI_OK;
}

static int cmd_pulse(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_write(c, "usage: pulse <microseconds>\r\n");
        return CLI_ERR_ARG;
    }
    pwm_out_set_pulse_us(&output, value);
    cli_printf(c, "%lu us of a %lu us frame\r\n", (unsigned long)value,
               (unsigned long)pwm_out_period_us(&output));
    return CLI_OK;
}

static int cmd_on(cli_t *c, void *user_data)
{
    (void)user_data;
    pwm_out_enable(&output, true);
    cli_write(c, "on\r\n");
    return CLI_OK;
}

static int cmd_off(cli_t *c, void *user_data)
{
    (void)user_data;
    pwm_out_enable(&output, false);
    cli_write(c, "off (the pin holds whatever level it stopped at)\r\n");
    return CLI_OK;
}

/* One ramp up and down. On an LED this is the visible test: a smooth fade with
   no step backwards is the duty arithmetic being monotonic. */
static int cmd_fade(cli_t *c, void *user_data)
{
    (void)user_data;
    for (uint32_t duty = 0; duty <= PWM_DUTY_MAX; duty += 256u) {
        pwm_out_set_duty(&output, (uint16_t)duty);
        sleep_ms(4);
    }
    for (uint32_t duty = PWM_DUTY_MAX; duty > 256u; duty -= 256u) {
        pwm_out_set_duty(&output, (uint16_t)duty);
        sleep_ms(4);
    }
    pwm_out_set_duty(&output, 0);
    cli_write(c, "faded up and down\r\n");
    return CLI_OK;
}

/* A servo sweep in the units a servo is specified in. Needs the 50 Hz profile
   to mean anything. */
static int cmd_servo(cli_t *c, void *user_data)
{
    (void)user_data;
    if (pwm_out_frequency(&output) > 400u) {
        cli_write(c, "this wants the servo profile (50 Hz); run freq 50 first\r\n");
        return CLI_ERR_STATE;
    }

    for (uint32_t us = 1000; us <= 2000; us += 10) {
        pwm_out_set_pulse_us(&output, us);
        sleep_ms(10);
    }
    for (uint32_t us = 2000; us >= 1010; us -= 10) {
        pwm_out_set_pulse_us(&output, us);
        sleep_ms(10);
    }
    pwm_out_set_pulse_us(&output, 1500);
    cli_write(c, "swept 1000-2000-1500 us\r\n");
    return CLI_OK;
}

/* ---------------------------------------------------------------------------
 * Slice sharing
 * -------------------------------------------------------------------------*/

static int cmd_second(cli_t *c, void *user_data)
{
    (void)user_data;

    cli_printf(c, "gpio %u and %u share a slice: %s\r\n",
               (unsigned)PWM_TEST_GPIO, (unsigned)PWM_TEST_SECOND_GPIO,
               pwm_gpio_shares_slice(PWM_TEST_GPIO, PWM_TEST_SECOND_GPIO) ? "yes" : "no");

    const pwm_out_config_t config = {
        .gpio = PWM_TEST_SECOND_GPIO,
        /* The same frequency, which is the only thing a shared slice allows. */
        .frequency_hz = output.config.frequency_hz,
        .duty = PWM_DUTY_MAX / 4u,
        .start_enabled = true,
    };

    const pwm_result_t result = pwm_out_init(&second, &config);
    cli_printf(c, "second output at the same frequency: %s\r\n", pwm_result_name(result));
    if (result == PWM_OK) {
        second_initialised = true;
        cli_write(c, "both channels now run, at 25% and whatever the first was\r\n");
    }
    return result == PWM_OK ? CLI_OK : CLI_ERR_FAILED;
}

/*
 * The safety net. Asking the paired GPIO for a different frequency has to be
 * refused: the slice has one wrap and one divider, so honouring it would move
 * the first output without telling anyone.
 */
static int cmd_conflict(cli_t *c, void *user_data)
{
    (void)user_data;

    const uint32_t other = output.config.frequency_hz * 2u;
    const pwm_out_config_t config = {
        .gpio = PWM_TEST_SECOND_GPIO,
        .frequency_hz = other,
        .duty = PWM_DUTY_MAX / 2u,
        .start_enabled = true,
    };

    pwm_out_t clashing;
    const pwm_result_t result = pwm_out_init(&clashing, &config);

    cli_printf(c, "gpio %u at %lu Hz while gpio %u runs at %lu Hz:\r\n",
               (unsigned)PWM_TEST_SECOND_GPIO, (unsigned long)other,
               (unsigned)PWM_TEST_GPIO, (unsigned long)output.config.frequency_hz);
    cli_printf(c, "  %s\r\n", pwm_result_name(result));

    if (result == PWM_ERR_SLICE_BUSY) {
        cli_write(c, "  correct: the first output was left alone\r\n");
        return CLI_OK;
    }
    cli_write(c, "  WRONG: this should have been refused\r\n");
    return CLI_ERR_FAILED;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "gpio       %u (slice %u)\r\n", (unsigned)output.config.gpio,
               (unsigned)pwm_out_slice(&output));
    cli_printf(c, "asked      %lu Hz\r\n", (unsigned long)output.config.frequency_hz);
    cli_printf(c, "running    %lu Hz\r\n", (unsigned long)pwm_out_frequency(&output));
    cli_printf(c, "period     %lu us\r\n", (unsigned long)pwm_out_period_us(&output));
    cli_printf(c, "resolution %lu counts\r\n", (unsigned long)pwm_out_resolution(&output));
    cli_printf(c, "duty       %u of %u\r\n", (unsigned)pwm_out_duty(&output),
               (unsigned)PWM_DUTY_MAX);
    cli_printf(c, "enabled    %s\r\n", pwm_out_is_enabled(&output) ? "yes" : "no");
    cli_printf(c, "second     %s\r\n", second_initialised ? "running" : "not started");
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "freq",     "freq [hz] - show or set the frequency",            cmd_freq,     NULL },
    { "duty",     "duty [0-65535] - show or set the duty cycle",      cmd_duty,     NULL },
    { "pct",      "pct <0-100> - set the duty as a percentage",       cmd_pct,      NULL },
    { "pulse",    "pulse <us> - set the high time, for a servo",      cmd_pulse,    NULL },
    { "on",       "on - start the counter",                           cmd_on,       NULL },
    { "off",      "off - stop the counter",                           cmd_off,      NULL },
    { "fade",     "fade - one ramp up and down",                      cmd_fade,     NULL },
    { "servo",    "servo - sweep 1000-2000 us (needs 50 Hz)",         cmd_servo,    NULL },
    { "second",   "second - start the paired GPIO at the same freq",  cmd_second,   NULL },
    { "conflict", "conflict - the paired GPIO at another freq: must refuse", cmd_conflict, NULL },
    { "status",   "status - pin, slice, frequency, resolution, duty", cmd_status,   NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const pwm_out_config_t config = {
        .gpio = PWM_TEST_GPIO,
        .frequency_hz = PWM_TEST_FREQUENCY_HZ,
        .duty = 0,                 /* off until told otherwise */
        .invert = false,
        .start_enabled = true,
    };

    const pwm_result_t started = pwm_out_init(&output, &config);

    size_t count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0; i < count_of(own_commands) && count < count_of(commands); i++) {
        commands[count++] = own_commands[i];
    }

    const cli_config_t cli_config = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "pwm> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\npwm_test  board %s  gpio %u\r\n", PICO_BOARD,
               (unsigned)PWM_TEST_GPIO);
    if (started != PWM_OK) {
        cli_printf(&cli, "pwm_out_init: %s\r\n", pwm_result_name(started));
    } else {
        cli_printf(&cli, "%lu Hz asked, %lu Hz running, %lu counts per period\r\n",
                   (unsigned long)PWM_TEST_FREQUENCY_HZ,
                   (unsigned long)pwm_out_frequency(&output),
                   (unsigned long)pwm_out_resolution(&output));
    }
    cli_write(&cli, "type help. Try pct 50, then fade\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);
        sleep_ms(1);
    }
}
