/*
 * bt_console_test - the framework's CLI, over Bluetooth.
 *
 * The point is how little there is here. The CLI, its built-in commands and
 * everything else are unchanged; only the cli_stream_t differs. That is what the
 * transport split in the cli component was for.
 *
 * See README.md for pairing.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "bt_console.h"
#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"

/*
 * Output gets the larger buffer. A `help` listing is a few hundred bytes and is
 * produced far faster than RFCOMM will take it.
 */
static uint8_t bt_incoming[256];
static uint8_t bt_outgoing[2048];

static bt_console_t console;
static char line_buffer[160];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 4u];

static int cmd_btstatus(cli_t *c, void *user_data)
{
    (void)user_data;

    cli_printf(c, "radio     %s\r\n",
               BT_CONSOLE_SUPPORTED ? "present" : "none on this board");
    cli_printf(c, "link      %s\r\n",
               bt_console_is_connected(&console) ? "connected" : "no peer");
    cli_printf(c, "dropped   %lu out, %lu in\r\n",
               (unsigned long)bt_console_dropped_output(&console),
               (unsigned long)bt_console_dropped_input(&console));
    return CLI_OK;
}

/* Produces far more output than one RFCOMM packet, which is the interesting
   case for the flow control. */
static int cmd_flood(cli_t *c, void *user_data)
{
    (void)user_data;

    uint32_t lines = 40;
    if (!cli_args_exhausted(c) && (!cli_next_u32(c, &lines) || lines == 0 ||
                                   lines > 500)) {
        cli_write(c, "usage: flood [lines 1-500]\r\n");
        return CLI_ERR_ARG;
    }

    for (uint32_t i = 0; i < lines; i++) {
        cli_printf(c, "line %lu of %lu: the quick brown fox jumps over the lazy dog\r\n",
                   (unsigned long)(i + 1), (unsigned long)lines);

        /*
         * Without this, the whole loop runs before BTstack ever gets a turn to
         * drain the RFCOMM link, so it all lands in the 2 KB buffer at once and
         * almost none of it survives — not the flow-control case this command
         * exists to exercise. Polling alone is not enough: RFCOMM_EVENT_CAN_SEND_NOW
         * only arrives after a real HCI round trip with the controller, so this
         * also has to let real time pass, at the same 1 ms cadence as the main
         * loop, for that round trip to land between prints.
         */
        bt_console_poll(&console);
        sleep_ms(1);
    }

    /*
     * Reported afterwards because it is the useful number: output produced
     * faster than the link drains it has to go somewhere, and this says whether
     * the buffer was big enough.
     */
    cli_printf(c, "done; %lu bytes dropped for want of buffer\r\n",
               (unsigned long)bt_console_dropped_output(&console));
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "btstatus", "link state and dropped bytes",     cmd_btstatus, NULL },
    { "flood",    "flood [lines] - lots of output",   cmd_flood,    NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const bt_console_config_t bt_config = {
        .name = "pico-framework",
        .incoming = bt_incoming,
        .incoming_size = sizeof(bt_incoming),
        .outgoing = bt_outgoing,
        .outgoing_size = sizeof(bt_outgoing),
        .discoverable = true,
    };

    const bt_console_result_t started = bt_console_init(&console, &bt_config);

    /* Reported over USB, since if Bluetooth did not start there is nowhere else
       to say so. */
    printf("bt_console_test  board %s  radio %s\n", PICO_BOARD,
           BT_CONSOLE_SUPPORTED ? "present" : "none");
    printf("bt_console_init: %s\n", bt_console_result_name(started));
    if (started == BT_CONSOLE_OK) {
        printf("pair with \"%s\", then open the serial port it offers\n",
               bt_config.name);
    }

    size_t count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0; i < count_of(own_commands) && count < count_of(commands); i++) {
        commands[count++] = own_commands[i];
    }

    /*
     * The only line that differs from a CLI over USB or a UART. Everything
     * above and below is the same.
     */
    const cli_config_t cli_config = {
        .commands = commands,
        .command_count = count,
        .stream = bt_console_stream(&console),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "bt> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    bool announced = false;

    while (true) {
        bt_console_poll(&console);
        cli_poll(&cli);

        /* Greet a peer when one attaches, so a terminal that connects to
           silence has something to show. */
        const bool connected = bt_console_is_connected(&console);
        if (connected && !announced) {
            cli_write(&cli, "\r\nbt_console_test - type help\r\n");
            cli_write_prompt(&cli);
            announced = true;
        } else if (!connected && announced) {
            announced = false;
            printf("bluetooth peer disconnected\n");
        }

        sleep_ms(1);
    }
}
