
/*
 * ==============================================================================
 * simple_robot - Example robot application built on pico-framework
 * ==============================================================================
 *
 * This application demonstrates the core features of the framework:
 *  1. Interactive Command-Line Interface (CLI) over a dedicated hardware UART.
 *  2. In-application firmware update service (Intel HEX upload over UART).
 *  3. Feetech Smart Serial Bus Servo communication (STS/SCS protocol over PIO).
 *  4. Addressable WS2812 RGB LED strip driven by a PIO state machine.
 *
 * Architecture Notes:
 *  - Memory model: Zero dynamic allocation (malloc). All buffers, drivers,
 *    and command tables are allocated statically in .bss / .data.
 *  - Non-blocking loop: The main loop polls the CLI and performs periodic
 *    tasks without blocking, ensuring reliable communication and updates.
 * ==============================================================================
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Pico SDK includes */
#include "pico/stdlib.h"
#include "hardware/uart.h"

/* pico-framework component headers */
#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "firmware_service.h"
#include "half_duplex_uart.h"
#include "feetech.h"
#include "ws2812.h"

/* ------------------------------------------------------------------------------
 * Global / Static State Structures (Static Allocation)
 * ------------------------------------------------------------------------------
 * All subsystem state is kept in static memory to guarantee zero heap fragmentation
 * and predictable RAM usage.
 * ------------------------------------------------------------------------------ */

/* CLI interpreter instance */
static cli_t cli;

/* Firmware update service state (handles flash staging and verification) */
static firmware_service_t firmware;

/* Single-wire half-duplex UART instance driven by RP2040 PIO */
static half_duplex_uart_t servo_uart;

/* Feetech servo protocol bus handle */
static servo_bus_t servo_bus;

/* WS2812 LED strip driver handle and caller-owned pixel buffer */
static ws2812_strip_t led_strip;
static ws2812_color_t led_pixels[APP_LED_COUNT];

/*
 * Line buffer for the CLI. Sized to 600 bytes to comfortably hold full
 * Intel HEX records during firmware upload.
 */
static char line_buffer[600];

/*
 * CLI command table combining:
 *  - Firmware update commands (fwstatus, fwapply, etc.)
 *  - CLI built-in commands (help, etc.)
 *  - Application-specific commands (led, servo_ping, etc.)
 */
static cli_command_t commands[
    FIRMWARE_SERVICE_MAX_COMMANDS +
    CLI_BUILTIN_COMMAND_COUNT +
    8
];

/* ------------------------------------------------------------------------------
 * Subsystem Initialization
 * ------------------------------------------------------------------------------ */

/**
 * @brief Initialize the WS2812 RGB LED strip driver.
 *
 * Configures a PIO state machine on pio1 to generate precise WS2812 timing signals
 * on APP_LED_PIN.
 */
static bool init_led_strip(void)
{
    const ws2812_config_t config = {
        .pio          = pio1,                    /* PIO instance to use (pio0 or pio1) */
        .pin          = APP_LED_PIN,             /* GPIO pin connected to LED DIN */
        .pixels       = led_pixels,              /* Pointer to caller-owned RGB pixel buffer */
        .length       = APP_LED_COUNT,           /* Number of LEDs */
        .is_rgbw      = false,                   /* false for standard RGB (WS2812B), true for RGBW */
        .frequency_hz = WS2812_DEFAULT_FREQUENCY_HZ, /* 800 kHz standard bit rate */
        .wire_buffer  = NULL,                    /* NULL for blocking show; set buffer for DMA async */
    };

    /* Claim state machine, load PIO program, and configure pin */
    if (ws2812_init(&led_strip, &config) != WS2812_OK) {
        return false;
    }

    /* Set default brightness (0-255) and turn off all LEDs initially */
    ws2812_set_brightness(&led_strip, 32);
    ws2812_clear(&led_strip);
    ws2812_show(&led_strip);
    return true;
}

/**
 * @brief Initialize the Feetech smart servo bus.
 *
 * Sets up a single-wire half-duplex UART on pio0 and initializes the Feetech
 * protocol handler (STS or SCS family).
 */
static bool init_servo_bus(void)
{
    const half_duplex_uart_config_t config = {
        .pio           = pio0,
        .pin           = APP_SERVO_PIN,
        .direction_pin = HALF_DUPLEX_UART_NO_DIRECTION_PIN, /* Single-wire bidirectional pin */
        .baudrate      = APP_SERVO_BAUD,
        .echo          = HALF_DUPLEX_UART_ECHO_DISCARD,     /* Discard own transmitted bytes */
    };

    if (half_duplex_uart_init(&servo_uart, &config) != HALF_DUPLEX_UART_OK) {
        return false;
    }

    /* Select STS (little-endian, default) or SCS (big-endian) protocol model */
    const feetech_model_t model =
        APP_FEETECH_SCS ? FEETECH_MODEL_SCS : FEETECH_MODEL_STS;

    return feetech_bus_init(&servo_bus, &servo_uart, model) == SERVO_BUS_OK;
}

/**
 * @brief Discard bytes produced while the board and USB-UART adapter settle.
 *
 * UART0 is configured before the other components, so clearing its FIFO at
 * that point is too early: an FTDI adapter can still glitch or finish sending
 * stale bytes during the rest of startup. Wait briefly, then drain immediately
 * before the CLI starts accepting a line.
 */
static void discard_startup_uart_input(uart_inst_t *uart)
{
    sleep_ms(APP_CLI_STARTUP_DRAIN_MS);
    while (uart_is_readable(uart)) {
        (void)uart_getc(uart);
    }
}

/* ------------------------------------------------------------------------------
 * CLI Command Callbacks
 * ------------------------------------------------------------------------------ */

/**
 * @brief Command handler for 'led <color>'.
 * Usage: led off|red|green|blue
 */
static int cmd_led(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *name = cli_next_token(c);
    if (name == NULL) {
        cli_write(c, "usage: led off|red|green|blue\r\n");
        return CLI_ERR_ARG;
    }

    ws2812_color_t color;

    if (strcmp(name, "off") == 0) {
        color = WS2812_COLOR_BLACK;
    } else if (strcmp(name, "red") == 0) {
        color = WS2812_COLOR_RED;
    } else if (strcmp(name, "green") == 0) {
        color = WS2812_COLOR_GREEN;
    } else if (strcmp(name, "blue") == 0) {
        color = WS2812_COLOR_BLUE;
    } else {
        cli_write(c, "expected off, red, green or blue\r\n");
        return CLI_ERR_ARG;
    }

    /* Fill the strip buffer with the requested color and send to hardware */
    ws2812_fill(&led_strip, color);
    ws2812_show(&led_strip);
    return CLI_OK;
}

/**
 * @brief Command handler for 'servo_ping <id>'.
 * Usage: servo_ping <0..253>
 */
static int cmd_servo_ping(cli_t *c, void *user_data)
{
    (void)user_data;

    uint32_t id;
    if (!cli_next_u32(c, &id) || id > FEETECH_ID_MAX) {
        cli_printf(c, "usage: servo_ping <0..%u>\r\n",
                (unsigned)FEETECH_ID_MAX);
        return CLI_ERR_ARG;
    }

    const servo_bus_result_t result =
        feetech_ping(&servo_bus, (uint8_t)id);

    if (result != SERVO_BUS_OK) {
        cli_printf(c, "error: %s\r\n",
                servo_bus_result_name(result));
        return CLI_ERR_FAILED;
    }

    cli_printf(c, "servo %lu answered\r\n", (unsigned long)id);
    return CLI_OK;
}

/* Application command list */
static const cli_command_t app_commands[] = {
    { "led",          "led off|red|green|blue", cmd_led,        NULL },
    { "servo_ping",   "servo_ping <id>",        cmd_servo_ping, NULL },
};

/* ------------------------------------------------------------------------------
 * Main Entry Point
 * ------------------------------------------------------------------------------ */

int main(void)
{
    /* Initialize default stdio (USB CDC output for debugging) */
    stdio_init_all();
    sleep_ms(1000);

    /*
     * Initialize dedicated hardware UART for the CLI console.
     * TX: GPIO APP_CLI_TX_PIN, RX: GPIO APP_CLI_RX_PIN.
     * Enable internal pull-up on RX to prevent floating line noise.
     */
    uart_inst_t *console_uart = uart_get_instance(APP_CLI_UART_ID);
    uart_init(console_uart, APP_CLI_BAUD);
    gpio_set_function(APP_CLI_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(APP_CLI_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(APP_CLI_RX_PIN);

    /* Initialize hardware modules */
    if (!init_led_strip()) {
        printf("LED strip initialization failed\n");
    }

    if (!init_servo_bus()) {
        printf("Feetech bus initialization failed\n");
    }

    /* Initialize firmware update service (flash partition management) */
    if (!firmware_service_init(&firmware)) {
        printf("Firmware service initialization failed\n");
        while (true) {
            sleep_ms(1000);
        }
    }

    /* Populate the CLI command table */
    size_t count = firmware_service_commands(&firmware,
                                            commands,
                                            count_of(commands));

    count += cli_builtin_commands(&commands[count],
                                 count_of(commands) - count);

    for (size_t i = 0; i < count_of(app_commands); ++i) {
        commands[count++] = app_commands[i];
    }

    /* Synchronise at a clean line boundary after all startup work is done. */
    discard_startup_uart_input(console_uart);

    /* Configure the CLI stream and line filter */
    const cli_config_t config = {
        .commands              = commands,
        .command_count         = count,
        .stream                = cli_stream_uart(console_uart),
        .line_buffer           = line_buffer,
        .line_buffer_size      = sizeof(line_buffer),
        .prompt                = "robot> ",

        /* Disable echo so Intel HEX firmware upload isn't echoed back */
        .echo                  = false,
        .enable_help           = true,

        /* Route incoming Intel HEX records (starting with ':') to the updater */
        .line_filter           = firmware_service_line_filter,
        .line_filter_user_data = &firmware,
    };

    if (cli_init(&cli, &config) != CLI_INIT_OK) {
        printf("CLI initialization failed\n");
        while (true) {
            sleep_ms(1000);
        }
    }

    /* Print startup banner on the CLI */
    cli_printf(&cli, "\r\nsimple_robot build %s\r\n", APP_BUILD_STAMP);
    cli_write(&cli, "type help\r\n");
    cli_write_prompt(&cli);

    /* Main super-loop */
    while (true) {
        /* Poll the CLI for incoming characters/commands */
        cli_poll(&cli);

        /*
         * Place periodic non-blocking robot tasks, sensor polling, or LED animations here.
         * Keep loop iterations fast and non-blocking so the CLI remains responsive.
         */
        sleep_ms(1);
    }
}
