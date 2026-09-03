/*
 * settings - what survives a power cut, and the console commands that set it.
 *
 * Two kinds of thing, kept together because they share a flash store and a
 * save: the credentials and addresses typed in once during setup, and the
 * light's own state, which changes constantly and is written back on a delay.
 *
 * Nothing here is compiled in (DESIGN_DOC.md section 13). A board with no
 * stored settings comes up with a usable light and an idle radio, and says so
 * on the console.
 */

#ifndef HOME_LED_SETTINGS_H
#define HOME_LED_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "app.h"

/*
 * Open the flash store and adopt everything in it: the strings, and then the
 * light, restored with its fades already finished so the strip lights at the
 * stored values rather than ramping up to them.
 */
void settings_load(app_t *app, uint32_t now_ms);

/*
 * Write the light's current state and commit the store. Called from the
 * console's `save`, and by the main loop once the light has been quiet for
 * SAVE_QUIET_MS.
 *
 * The string settings are already in the store by the time this runs -- each
 * setting command puts them there -- so this is what makes them durable too.
 */
bool settings_store(app_t *app);

/* The nine setting commands plus `save`. */
#define SETTINGS_COMMAND_COUNT 10u

size_t settings_commands(app_t *app, cli_command_t *out, size_t capacity);

#endif /* HOME_LED_SETTINGS_H */
