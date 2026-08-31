/*
 * cli_builtins - the commands every firmware wants and nobody should write.
 *
 * ping, version, uptime, reboot, bootsel. Each one was being reimplemented in
 * every test application here, slightly differently, which is exactly the
 * duplication this framework exists to remove.
 *
 * They live beside cli.c rather than inside it because they need the Pico SDK —
 * the bootrom, the watchdog, the timer — and cli.c is deliberately free of it
 * so that the whole interpreter can be unit-tested on the host. The one
 * built-in that does live in cli.c is `help`, because enumerating the command
 * table is something only the interpreter can do.
 *
 * Registered the same way firmware_service_commands() is:
 *
 *     cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 2];
 *     size_t count = cli_builtin_commands(commands, count_of(commands));
 *     commands[count++] = (cli_command_t){ "mine", "...", cmd_mine, NULL };
 */

#ifndef PICO_FRAMEWORK_CLI_BUILTINS_H
#define PICO_FRAMEWORK_CLI_BUILTINS_H

#include <stddef.h>

#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many cli_builtin_commands() will write, for sizing an array. */
#define CLI_BUILTIN_COMMAND_COUNT 5u

/*
 * Fill `out` with the built-in commands and return how many were written,
 * which is fewer than CLI_BUILTIN_COMMAND_COUNT only if `capacity` is.
 *
 *   ping      answer with pong. The cheapest possible "is it alive".
 *   version   board, SDK version and build type
 *   uptime    milliseconds since reset
 *   reboot    restart the firmware
 *   bootsel   restart into the USB bootloader, so picotool can flash it
 *             without anyone pressing the button
 *
 * The two that restart the board reply first and then wait briefly, so the
 * reply reaches the other end before the link goes away.
 */
size_t cli_builtin_commands(cli_command_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CLI_BUILTINS_H */
