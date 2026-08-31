#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "cli_builtins.h"

/* Long enough for a reply to get out of a UART FIFO at 115200 before the board
   stops listening. */
#define GOODBYE_MS 100u

static int cmd_ping(cli_t *cli, void *user_data)
{
    (void)user_data;
    cli_write(cli, "pong\r\n");
    return CLI_OK;
}

static int cmd_version(cli_t *cli, void *user_data)
{
    (void)user_data;
    cli_printf(cli, "board  %s\r\n", PICO_BOARD);
    cli_printf(cli, "sdk    %s\r\n", PICO_SDK_VERSION_STRING);
    cli_printf(cli, "build  %s\r\n", PICO_CMAKE_BUILD_TYPE);
    return CLI_OK;
}

static int cmd_uptime(cli_t *cli, void *user_data)
{
    (void)user_data;

    const uint64_t ms = time_us_64() / 1000u;
    cli_printf(cli, "%llu ms\r\n", (unsigned long long)ms);
    return CLI_OK;
}

static int cmd_reboot(cli_t *cli, void *user_data)
{
    (void)user_data;

    cli_write(cli, "rebooting\r\n");
    sleep_ms(GOODBYE_MS);

    /* 0, 0 means "start the flash image normally"; the delay is how long the
       watchdog waits before doing it. */
    watchdog_reboot(0, 0, 1);

    while (true) {
        tight_loop_contents();
    }
    return CLI_OK; /* unreachable; the compiler wants it anyway */
}

static int cmd_bootsel(cli_t *cli, void *user_data)
{
    (void)user_data;

    cli_write(cli, "rebooting into the USB bootloader\r\n");
    sleep_ms(GOODBYE_MS);

    /*
     * The reason a board never needs its BOOTSEL button pressed: this drops
     * into the bootrom's USB mode, where picotool can flash it. The arguments
     * disable nothing and light no activity LED.
     */
    reset_usb_boot(0, 0);

    while (true) {
        tight_loop_contents();
    }
    return CLI_OK; /* unreachable; the compiler wants it anyway */
}

size_t cli_builtin_commands(cli_command_t *out, size_t capacity)
{
    if (out == NULL) {
        return 0;
    }

    static const cli_command_t table[] = {
        { "ping",    "answer with pong",                      cmd_ping,    NULL },
        { "version", "board, SDK version and build type",     cmd_version, NULL },
        { "uptime",  "milliseconds since reset",              cmd_uptime,  NULL },
        { "reboot",  "restart the firmware",                  cmd_reboot,  NULL },
        { "bootsel", "restart into the USB bootloader",       cmd_bootsel, NULL },
    };

    _Static_assert(count_of(table) == CLI_BUILTIN_COMMAND_COUNT,
                   "CLI_BUILTIN_COMMAND_COUNT must match the table");

    const size_t count = (capacity < count_of(table)) ? capacity : count_of(table);
    for (size_t i = 0; i < count; i++) {
        out[i] = table[i];
    }
    return count;
}
