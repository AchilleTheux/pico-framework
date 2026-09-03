/*
 * ha - the Home Assistant side of the light: its topics, its discovery
 * document, and the JSON it exchanges.
 *
 * This is the only file that knows Home Assistant exists. `light.h` is the
 * model, `effects.h` draws it, and this translates between that model and one
 * particular controller's idea of a light. A second controller would be
 * another file like this one, not a change to either of those.
 *
 * Built on the framework's `json` component, so the parsing is a real scan
 * over the payload's keys rather than a search for a substring -- which
 * matters here more than anywhere: an effect named "brightness test" would
 * otherwise be read as a brightness.
 *
 * NO PICO SDK, AND NO MQTT
 *
 * Nothing here publishes or subscribes. Building a payload and deciding what
 * an incoming one means are separable from moving bytes, and keeping them
 * separate is what lets the whole schema be exercised on the host: main.c
 * hands a payload in and takes one out. (DESIGN_DOC.md section 19.)
 *
 * DISCOVERY
 *
 * Home Assistant finds MQTT devices by their retained announcement on
 * `<prefix>/light/<id>/config`. It has to be republished on every reconnect --
 * see mqtt's on_connect -- because a broker with a clean session will not have
 * kept this client's, and a restarted Home Assistant reads only what is
 * retained.
 */

#ifndef HOME_LED_HA_H
#define HOME_LED_HA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "light.h"

/* Long enough for "homeassistant/light/<id>/status" with room to spare, and
   short enough to sit in mqtt's MQTT_TOPIC_MAX_LENGTH. */
#define HA_TOPIC_MAX_LENGTH 96u
#define HA_DEVICE_ID_MAX_LENGTH 32u

/*
 * A discovery document with the full-length key names runs to roughly 600
 * bytes. lwIP's MQTT output ring buffer is what limits this in practice --
 * see components/wifi/include/lwipopts.h, which raises it to 1 KiB precisely
 * so this fits.
 */
#define HA_DISCOVERY_BUFFER_SIZE 1024u
#define HA_STATE_BUFFER_SIZE 256u

/* What a device publishes to say it is reachable. Also the will message, so
   the broker says it for us when the board disappears without warning. */
#define HA_AVAILABLE "online"
#define HA_UNAVAILABLE "offline"

typedef struct {
    char device_id[HA_DEVICE_ID_MAX_LENGTH + 1];

    /* Derived once at init, because they are used on every publish and
       rebuilding them each time is a printf per message for no reason. */
    char topic_config[HA_TOPIC_MAX_LENGTH + 1];
    char topic_command[HA_TOPIC_MAX_LENGTH + 1];
    char topic_state[HA_TOPIC_MAX_LENGTH + 1];
    char topic_availability[HA_TOPIC_MAX_LENGTH + 1];
} ha_t;

/*
 * Derive the four topics from a device id.
 *
 * `discovery_prefix` is Home Assistant's, "homeassistant" unless it has been
 * changed there; NULL selects that default. False if the id is empty or the
 * topics would not fit, rather than quietly truncating them into something
 * that half works.
 */
bool ha_init(ha_t *ha, const char *device_id, const char *discovery_prefix);

/* ---------------------------------------------------------------------------
 * Outgoing
 * -------------------------------------------------------------------------*/

/*
 * The retained announcement: what this device is, which topics it speaks, and
 * what it can do. Returns the length written, or 0 if it did not fit -- there
 * is no half-document worth publishing.
 *
 * Publish it retained, on every accepted broker session.
 */
size_t ha_build_discovery(const ha_t *ha, char *out, size_t size);

/*
 * The light's current settings, in the shape Home Assistant's JSON schema
 * expects. Reports what has been *asked for* rather than what a fade has
 * reached so far: a controller showing a slider wants the value it set, not a
 * number sliding under it for three seconds.
 */
size_t ha_build_state(const ha_t *ha, const light_t *light, char *out, size_t size);

/* ---------------------------------------------------------------------------
 * Incoming
 * -------------------------------------------------------------------------*/

/*
 * One command from `<prefix>/light/<id>/set`.
 *
 * Every field is optional and flagged, because Home Assistant sends only what
 * changed -- a brightness drag is `{"brightness":140}` with nothing else, and
 * treating the absent fields as zero would switch the light off and turn it
 * white on every step.
 */
typedef struct {
    bool has_power;
    bool power;

    bool has_brightness;
    uint8_t brightness;

    bool has_color;
    ws2812_color_t color;

    bool has_mireds;
    uint16_t mireds;

    bool has_effect;
    light_effect_t effect;

    /*
     * An effect name arrived that this device does not offer. Kept separate
     * from has_effect so the caller can say so rather than silently doing
     * nothing -- it usually means the effect list and the code disagree.
     */
    bool unknown_effect;
} ha_command_t;

/*
 * Parse a payload. `length` bounds it, since an MQTT payload is a pointer and
 * a length and is not zero-terminated.
 *
 * False when the payload is not a JSON object at all, or is cut short. True
 * with every flag clear is a valid document that asked for nothing, which is
 * not an error.
 */
bool ha_parse_command(const char *payload, size_t length, ha_command_t *out);

/*
 * Apply whichever fields were present, in the order Home Assistant means them:
 * colour and effect before power, so a light told to switch on and turn red in
 * one message does not show its old colour first.
 */
void ha_apply_command(const ha_command_t *command, light_t *light, uint32_t now_ms);

#endif /* HOME_LED_HA_H */
