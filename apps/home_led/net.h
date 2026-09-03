/*
 * net - the link, the broker session, and the Home Assistant conversation.
 *
 * Three layers that only make sense together: `wifi` gets an address, `mqtt`
 * gets a session on top of it, and `ha` decides what to say. This file is the
 * wiring between them and the light.
 *
 * Nothing here blocks. Both components are poll-driven and net_poll() is
 * called every time round the main loop.
 */

#ifndef HOME_LED_NET_H
#define HOME_LED_NET_H

#include <stddef.h>

#include "app.h"

/* Bring up the radio, the MQTT client and the Home Assistant topics. Failures
   are recorded in the app rather than fatal -- a board with no radio still
   has a working light and a console to say so from. */
void net_init(app_t *app);

/* Associate and connect if there are stored settings to do it with, so a
   deployed board needs no console after the first setup. */
void net_start_if_configured(app_t *app);

/* Drive both state machines, and report state changes to the console. */
void net_poll(app_t *app);

/*
 * Publish the light's state, retained. Does nothing without a session, which
 * is why the main loop's save timer is driven by the light's generation
 * counter and not by this.
 */
void net_publish_state(app_t *app);

/*
 * Everything a new broker session has to re-establish: the subscription, the
 * retained discovery document, the availability message and the state.
 *
 * Registered as mqtt's on_connect, so it runs on the first session and every
 * reconnection -- lwIP always connects with the clean-session flag set, so
 * the broker forgets the subscription each time the link drops. Also reachable
 * from the console's `announce`, for when Home Assistant has been reinstalled
 * and its retained copy is gone.
 */
void net_announce(app_t *app);

/* `connect`, `disconnect` and `announce`. */
#define NET_COMMAND_COUNT 3u

size_t net_commands(app_t *app, cli_command_t *out, size_t capacity);

#endif /* HOME_LED_NET_H */
