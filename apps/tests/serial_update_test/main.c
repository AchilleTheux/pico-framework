/*
 * serial_update_test - a firmware that can be replaced over its own console.
 *
 * The reference implementation of the feature: a CLI with the firmware update
 * service registered, so the same serial link carries both ordinary commands
 * and a new image.
 *
 * It prints a build stamp at startup and from `version`, which is how you tell
 * whether an update actually took: flash it once, note the stamp, send a
 * rebuilt image, and check the stamp changed.
 *
 * See README.md. The install step is compiled out unless the profile enables
 * it; read apps/tests/serial_update_test/README.md and firmware_apply.h before
 * turning that on.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "firmware_service.h"

/* Set from the build so a rebuilt image is distinguishable from this one. */
#ifndef SERIAL_UPDATE_BUILD_STAMP
#define SERIAL_UPDATE_BUILD_STAMP "unset"
#endif

static firmware_service_t service;
static cli_t cli;
static char line_buffer[600];   /* a HEX record can be 521 characters */

static cli_command_t commands[FIRMWARE_SERVICE_MAX_COMMANDS +
                              CLI_BUILTIN_COMMAND_COUNT + 1u];

/*
 * The build stamp, which is how an update is seen to have taken effect. Named
 * `build` rather than `version` so it sits alongside the built-in `version`
 * rather than clashing with it — cli_init() refuses duplicate names.
 */
static int cmd_build(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "stamp   %s\r\n", SERIAL_UPDATE_BUILD_STAMP);
    cli_printf(c, "apply   %s\r\n",
               FIRMWARE_SERVICE_ENABLE_APPLY ? "enabled" : "not built");
    return CLI_OK;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    if (!firmware_service_init(&service)) {
        while (true) {
            printf("firmware_service_init failed\n");
            sleep_ms(1000);
        }
    }

    /* The update service, the framework's built-ins, then this application's
       own — none of which has to reimplement ping or reboot. */
    size_t count = firmware_service_commands(&service, commands, count_of(commands));
    count += cli_builtin_commands(&commands[count], count_of(commands) - count);
    commands[count++] = (cli_command_t){ "build", "build stamp of this firmware",
                                         cmd_build, NULL };

    const cli_config_t config = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "> ",

        /*
         * Echo off. A firmware image is tens of thousands of lines, and
         * echoing each one back doubles the traffic on the link that is
         * already the slow part of the transfer.
         */
        .echo = false,
        .enable_help = true,

        /* What routes ':' records to the updater instead of reporting them as
           unknown commands. */
        .line_filter = firmware_service_line_filter,
        .line_filter_user_data = &service,
    };

    if (cli_init(&cli, &config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nserial_update_test  build %s  board %s\r\n",
               SERIAL_UPDATE_BUILD_STAMP, PICO_BOARD);
    cli_write(&cli, "type help\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);
        sleep_ms(1);
    }
}
