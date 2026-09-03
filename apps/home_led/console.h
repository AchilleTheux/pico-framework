/*
 * console - the command table, and the CLI it is registered with.
 *
 * Assembles four sets of commands onto one interpreter: the framework's
 * built-ins, the firmware updater's, the settings commands, and the light's
 * own. Everything an interactive session needs is configured here -- history,
 * echo, and the raw-line prefix that keeps a firmware transfer from fighting
 * with a person typing.
 */

#ifndef HOME_LED_CONSOLE_H
#define HOME_LED_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

#include "app.h"

/*
 * Build the command table and start the interpreter. False if the table did
 * not fit or cli_init() refused it, which is a programming error rather than
 * a runtime condition.
 */
bool console_init(app_t *app);

/* What the board says once, on startup. */
void console_banner(app_t *app);

/* The light's own commands: on, off, bri, rgb, ct, effect, test, order,
   status. */
#define CONSOLE_LIGHT_COMMAND_COUNT 9u

size_t console_light_commands(app_t *app, cli_command_t *out, size_t capacity);

#endif /* HOME_LED_CONSOLE_H */
