/*
 * cli_test - hardware test for the cli component.
 *
 * Registers a handful of commands that exercise every argument type and both
 * transports, so a serial terminal is enough to check the interpreter on real
 * hardware. See README.md beside this file.
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "cli.h"
#include "cli_stream.h"

/* Overridable from the profiles under profiles/tests/cli_test. */
#ifndef CLI_TEST_USE_UART
#define CLI_TEST_USE_UART 0
#endif

#ifndef CLI_TEST_UART_ID
#define CLI_TEST_UART_ID 0
#endif

#ifndef CLI_TEST_UART_TX_PIN
#define CLI_TEST_UART_TX_PIN 0
#endif

#ifndef CLI_TEST_UART_RX_PIN
#define CLI_TEST_UART_RX_PIN 1
#endif

#ifndef CLI_TEST_UART_BAUD
#define CLI_TEST_UART_BAUD 115200
#endif

#ifndef CLI_TEST_ECHO
#define CLI_TEST_ECHO 1
#endif

static char line_buffer[128];
static cli_t cli;

/* ------------------------------------------------------------------------ */

static int cmd_ping(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_write(c, "pong\r\n");
    return CLI_OK;
}

static int cmd_info(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "board   %s\r\n", PICO_BOARD);
    cli_printf(c, "sdk     %s\r\n", PICO_SDK_VERSION_STRING);
    cli_printf(c, "uptime  %llu ms\r\n", (unsigned long long)(time_us_64() / 1000));
    cli_printf(c, "transport %s\r\n", CLI_TEST_USE_UART ? "uart" : "stdio");
    return CLI_OK;
}

#ifdef PICO_DEFAULT_LED_PIN
static int cmd_led(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *state = cli_next_token(c);
    if (state == NULL) {
        cli_write(c, "usage: led on|off\r\n");
        return CLI_ERR_ARG;
    }

    if (state[0] == 'o' && state[1] == 'n' && state[2] == '\0') {
        gpio_put(PICO_DEFAULT_LED_PIN, true);
    } else if (state[0] == 'o' && state[1] == 'f' && state[2] == 'f' && state[3] == '\0') {
        gpio_put(PICO_DEFAULT_LED_PIN, false);
    } else {
        cli_printf(c, "expected on or off, got %s\r\n", state);
        return CLI_ERR_ARG;
    }
    return CLI_OK;
}
#endif

/* Exercises unsigned parsing and the range error path. */
static int cmd_add(cli_t *c, void *user_data)
{
    (void)user_data;

    uint32_t a, b;
    if (!cli_next_u32(c, &a) || !cli_next_u32(c, &b)) {
        cli_write(c, "usage: add <a> <b>   (decimal, or 0x-prefixed)\r\n");
        return CLI_ERR_ARG;
    }
    if (a > UINT32_MAX - b) {
        return CLI_ERR_RANGE;
    }

    cli_printf(c, "%lu\r\n", (unsigned long)(a + b));
    return CLI_OK;
}

/* Exercises signed, hex and float parsing in one line. */
static int cmd_parse(cli_t *c, void *user_data)
{
    (void)user_data;

    int32_t i;
    uint32_t h;
    float f;

    if (!cli_next_i32(c, &i) || !cli_next_hex32(c, &h) || !cli_next_float(c, &f)) {
        cli_write(c, "usage: parse <signed> <hex> <float>\r\n");
        return CLI_ERR_ARG;
    }

    cli_printf(c, "i32=%ld hex=0x%08lX float=%.4f\r\n",
               (long)i, (unsigned long)h, (double)f);
    return CLI_OK;
}

/* Exercises free-text capture, which must be the last argument. */
static int cmd_echo(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *rest = cli_rest(c);
    cli_printf(c, "[%s]\r\n", rest != NULL ? rest : "");
    return CLI_OK;
}

/* Always fails, so the error-reporting path is visible on hardware. */
static int cmd_fail(cli_t *c, void *user_data)
{
    (void)c;
    (void)user_data;
    return CLI_ERR_FAILED;
}

static int cmd_bootsel(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_write(c, "rebooting into BOOTSEL\r\n");
    sleep_ms(100);
    reset_usb_boot(0, 0);
    return CLI_OK;
}

static const cli_command_t commands[] = {
    { "ping",    "answer with pong",                    cmd_ping,    NULL },
    { "info",    "board, SDK version and uptime",       cmd_info,    NULL },
#ifdef PICO_DEFAULT_LED_PIN
    { "led",     "led on|off",                          cmd_led,     NULL },
#endif
    { "add",     "add <a> <b>, decimal or 0x-prefixed", cmd_add,     NULL },
    { "parse",   "parse <signed> <hex> <float>",        cmd_parse,   NULL },
    { "echo",    "echo the rest of the line",           cmd_echo,    NULL },
    { "fail",    "always returns an error",             cmd_fail,    NULL },
    { "bootsel", "reboot into the USB bootloader",      cmd_bootsel, NULL },
};

/* ------------------------------------------------------------------------ */

static cli_stream_t open_stream(void)
{
#if CLI_TEST_USE_UART
    uart_inst_t *uart = uart_get_instance(CLI_TEST_UART_ID);

    /* The application owns the UART; the transport only reads and writes it. */
    uart_init(uart, CLI_TEST_UART_BAUD);
    gpio_set_function(CLI_TEST_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(CLI_TEST_UART_RX_PIN, GPIO_FUNC_UART);

    return cli_stream_uart(uart);
#else
    return cli_stream_stdio();
#endif
}

int main(void)
{
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    /* Give a USB console a moment to attach before the banner. */
    sleep_ms(2000);

    const cli_config_t config = {
        .commands = commands,
        .command_count = count_of(commands),
        .stream = open_stream(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "> ",
        .echo = CLI_TEST_ECHO,
        .enable_help = true,
    };

    if (cli_init(&cli, &config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_write(&cli, "\r\ncli_test - type help\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);

        /* The CLI never blocks, so the rest of a real main loop would run
           here. Sleeping only keeps this test from spinning the core flat. */
        sleep_ms(1);
    }
}
