#include "ha.h"

#include <stdio.h>
#include <string.h>

#include "json.h"

#define DEFAULT_DISCOVERY_PREFIX "homeassistant"

/* ---------------------------------------------------------------------------
 * Topics
 * -------------------------------------------------------------------------*/

/* snprintf into a fixed buffer, reporting truncation rather than hiding it. */
static bool compose(char *out, size_t size, const char *prefix, const char *component,
                    const char *id, const char *suffix)
{
    const int written = snprintf(out, size, "%s/%s/%s/%s", prefix, component, id, suffix);

    return written > 0 && (size_t)written < size;
}

static bool compose_range_config(char *out, size_t size, const char *prefix,
                                 const char *id, const char *endpoint)
{
    const int written = snprintf(out, size, "%s/number/%s_range_%s/config",
                                 prefix, id, endpoint);

    return written > 0 && (size_t)written < size;
}

bool ha_init(ha_t *ha, const char *device_id, const char *discovery_prefix)
{
    if (ha == NULL || device_id == NULL || device_id[0] == '\0') {
        return false;
    }

    memset(ha, 0, sizeof(*ha));

    if (strlen(device_id) > HA_DEVICE_ID_MAX_LENGTH) {
        return false;
    }
    snprintf(ha->device_id, sizeof(ha->device_id), "%s", device_id);

    const char *prefix = (discovery_prefix != NULL && discovery_prefix[0] != '\0')
        ? discovery_prefix
        : DEFAULT_DISCOVERY_PREFIX;

    /*
     * All ten or none. A device with a working command topic and a truncated
     * state topic looks like it is ignoring Home Assistant, which is a much
     * harder thing to diagnose than a refusal at startup.
     */
    if (!compose(ha->topic_config, sizeof(ha->topic_config), prefix, "light",
                 device_id, "config") ||
        !compose(ha->topic_command, sizeof(ha->topic_command), prefix, "light",
                 device_id, "set") ||
        !compose(ha->topic_state, sizeof(ha->topic_state), prefix, "light",
                 device_id, "state") ||
        !compose(ha->topic_availability, sizeof(ha->topic_availability), prefix, "light",
                 device_id, "status") ||
        !compose_range_config(ha->topic_range_first_config,
                              sizeof(ha->topic_range_first_config), prefix,
                              device_id, "first") ||
        !compose(ha->topic_range_first_command, sizeof(ha->topic_range_first_command),
                 prefix, "light", device_id, "range/first/set") ||
        !compose(ha->topic_range_first_state, sizeof(ha->topic_range_first_state),
                 prefix, "light", device_id, "range/first/state") ||
        !compose_range_config(ha->topic_range_last_config,
                              sizeof(ha->topic_range_last_config), prefix,
                              device_id, "last") ||
        !compose(ha->topic_range_last_command, sizeof(ha->topic_range_last_command),
                 prefix, "light", device_id, "range/last/set") ||
        !compose(ha->topic_range_last_state, sizeof(ha->topic_range_last_state),
                 prefix, "light", device_id, "range/last/state")) {
        memset(ha, 0, sizeof(*ha));
        return false;
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

static void write_device(json_writer_t *writer, const ha_t *ha)
{
    json_writer_object_open(writer, "device");
    json_writer_array_open(writer, "identifiers");
    json_writer_string(writer, NULL, ha->device_id);
    json_writer_array_close(writer);
    json_writer_string(writer, "name", "Pico LED controller");
    json_writer_string(writer, "manufacturer", "pico-framework");
    json_writer_string(writer, "model", "home_led");
    json_writer_object_close(writer);
}

size_t ha_build_discovery(const ha_t *ha, char *out, size_t size)
{
    if (ha == NULL || out == NULL || size == 0 || ha->device_id[0] == '\0') {
        return 0;
    }

    char unique_id[HA_DEVICE_ID_MAX_LENGTH + 8u];
    snprintf(unique_id, sizeof(unique_id), "%s_light", ha->device_id);

    json_writer_t writer;
    json_writer_init(&writer, out, size);

    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "name", "LED strip");
    json_writer_string(&writer, "unique_id", unique_id);

    /* "json" is what makes Home Assistant send whole documents rather than
       one-value payloads, which is the schema everything below assumes. */
    json_writer_string(&writer, "schema", "json");

    json_writer_string(&writer, "command_topic", ha->topic_command);
    json_writer_string(&writer, "state_topic", ha->topic_state);
    json_writer_string(&writer, "availability_topic", ha->topic_availability);
    json_writer_string(&writer, "payload_available", HA_AVAILABLE);
    json_writer_string(&writer, "payload_not_available", HA_UNAVAILABLE);

    json_writer_bool(&writer, "brightness", true);

    json_writer_array_open(&writer, "supported_color_modes");
    json_writer_string(&writer, NULL, "rgb");
    json_writer_string(&writer, NULL, "color_temp");
    json_writer_array_close(&writer);

    json_writer_int(&writer, "min_mireds", (int32_t)LIGHT_MIREDS_MIN);
    json_writer_int(&writer, "max_mireds", (int32_t)LIGHT_MIREDS_MAX);

    json_writer_bool(&writer, "effect", true);
    json_writer_array_open(&writer, "effect_list");
    for (unsigned i = 0; i < (unsigned)LIGHT_EFFECT_COUNT; i++) {
        json_writer_string(&writer, NULL, light_effect_name((light_effect_t)i));
    }
    json_writer_array_close(&writer);

    /* QoS 1 so a command is not lost to a single dropped packet; the light is
       idempotent, so a duplicate costs nothing. */
    json_writer_int(&writer, "qos", 1);

    /*
     * The device block is what makes Home Assistant group this light under one
     * device rather than listing a bare entity. `identifiers` is the key it
     * matches on across restarts.
     */
    write_device(&writer, ha);

    json_writer_object_close(&writer);

    return json_writer_finish(&writer) ? json_writer_length(&writer) : 0;
}

size_t ha_build_range_discovery(const ha_t *ha, ha_range_endpoint_t endpoint,
                                uint16_t led_count, char *out, size_t size)
{
    if (ha == NULL || out == NULL || size == 0u || led_count == 0u ||
        ha->device_id[0] == '\0' ||
        (endpoint != HA_RANGE_FIRST && endpoint != HA_RANGE_LAST)) {
        return 0;
    }

    const bool first = endpoint == HA_RANGE_FIRST;
    const char *name = first ? "Range start" : "Range end";
    const char *command_topic = first ? ha->topic_range_first_command
                                      : ha->topic_range_last_command;
    const char *state_topic = first ? ha->topic_range_first_state
                                    : ha->topic_range_last_state;
    char unique_id[HA_DEVICE_ID_MAX_LENGTH + 20u];
    snprintf(unique_id, sizeof(unique_id), "%s_range_%s", ha->device_id,
             first ? "first" : "last");

    json_writer_t writer;
    json_writer_init(&writer, out, size);

    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "name", name);
    json_writer_string(&writer, "unique_id", unique_id);
    json_writer_string(&writer, "command_topic", command_topic);
    json_writer_string(&writer, "state_topic", state_topic);
    json_writer_string(&writer, "availability_topic", ha->topic_availability);
    json_writer_string(&writer, "payload_available", HA_AVAILABLE);
    json_writer_string(&writer, "payload_not_available", HA_UNAVAILABLE);
    json_writer_int(&writer, "min", 1);
    json_writer_int(&writer, "max", (int32_t)led_count);
    json_writer_int(&writer, "step", 1);
    json_writer_string(&writer, "mode", "slider");
    json_writer_int(&writer, "qos", 1);
    write_device(&writer, ha);
    json_writer_object_close(&writer);

    return json_writer_finish(&writer) ? json_writer_length(&writer) : 0;
}

/* ---------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------*/

size_t ha_build_state(const ha_t *ha, const light_t *light, char *out, size_t size)
{
    if (ha == NULL || light == NULL || out == NULL || size == 0) {
        return 0;
    }

    json_writer_t writer;
    json_writer_init(&writer, out, size);

    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "state", light->on ? "ON" : "OFF");

    /*
     * The requested values, not the faded ones. A controller that set the
     * brightness to 200 should see 200 straight away; reporting the ramp would
     * make its slider crawl for three seconds after every change and fight
     * whatever the user does next.
     */
    json_writer_int(&writer, "brightness", (int32_t)light->brightness);
    json_writer_string(&writer, "color_mode", light_color_mode_name(light->color_mode));

    /* Exactly one of these, matching the mode just reported: Home Assistant
       treats an attribute that does not belong to the active colour mode as a
       contradiction. */
    if (light->color_mode == LIGHT_COLOR_MODE_TEMP) {
        json_writer_int(&writer, "color_temp", (int32_t)light->mireds);
    } else {
        json_writer_object_open(&writer, "color");
        json_writer_int(&writer, "r", light->color.r);
        json_writer_int(&writer, "g", light->color.g);
        json_writer_int(&writer, "b", light->color.b);
        json_writer_object_close(&writer);
    }

    const char *effect = light_effect_name(light->effect);
    if (effect != NULL) {
        json_writer_string(&writer, "effect", effect);
    }

    json_writer_object_close(&writer);

    return json_writer_finish(&writer) ? json_writer_length(&writer) : 0;
}

size_t ha_build_range_state(const led_range_t *range, ha_range_endpoint_t endpoint,
                            char *out, size_t size)
{
    if (range == NULL || out == NULL || size == 0u ||
        (endpoint != HA_RANGE_FIRST && endpoint != HA_RANGE_LAST)) {
        return 0;
    }

    const unsigned value = endpoint == HA_RANGE_FIRST
        ? (unsigned)led_range_first(range)
        : (unsigned)led_range_last(range);
    const int written = snprintf(out, size, "%u", value);

    return written > 0 && (size_t)written < size ? (size_t)written : 0u;
}

/* ---------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------*/

/* Read one 0..255 channel out of a colour object. */
static bool read_channel(const json_value_t *object, const char *key, uint8_t *out)
{
    json_value_t value;
    int32_t number;

    if (!json_find_in(object, key, &value) || !json_get_int(&value, &number)) {
        return false;
    }
    if (number < 0) {
        number = 0;
    } else if (number > 255) {
        number = 255;
    }
    *out = (uint8_t)number;
    return true;
}

/* True when the first thing in the payload is the opening brace of an
   object, whitespace aside. */
static bool starts_an_object(const char *payload, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        const char c = payload[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        return c == '{';
    }
    return false;
}

bool ha_parse_command(const char *payload, size_t length, ha_command_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (payload == NULL || length == 0) {
        return false;
    }

    json_value_t value;

    /*
     * Check the shape once up front, so a payload that is not a command at all
     * is reported as a failure rather than as a command that happened to ask
     * for nothing.
     *
     * Both halves are needed. json_valid_n() alone accepts `[1,2]` and `42` --
     * valid JSON, but not something with fields to read, and every lookup
     * below would simply come back empty. Requiring an object alone would
     * accept one cut short in transit. Together they mean: a whole object, or
     * nothing.
     */
    if (!starts_an_object(payload, length) || !json_valid_n(payload, length)) {
        return false;
    }

    /* From here on every field is optional: Home Assistant sends only what
       changed, and the absent ones must stay absent rather than defaulting. */
    if (json_find_n(payload, length, "state", &value)) {
        if (json_string_equals(&value, "ON")) {
            out->has_power = true;
            out->power = true;
        } else if (json_string_equals(&value, "OFF")) {
            out->has_power = true;
            out->power = false;
        }
    }

    if (json_find_n(payload, length, "brightness", &value)) {
        int32_t number;

        if (json_get_int(&value, &number)) {
            if (number < 0) {
                number = 0;
            } else if (number > 255) {
                number = 255;
            }
            out->has_brightness = true;
            out->brightness = (uint8_t)number;
        }
    }

    if (json_find_n(payload, length, "color", &value)) {
        uint8_t r;
        uint8_t g;
        uint8_t b;

        /* All three or none: two channels of a colour is not a colour, and
           filling the third in with zero would be inventing one. */
        if (read_channel(&value, "r", &r) && read_channel(&value, "g", &g) &&
            read_channel(&value, "b", &b)) {
            out->has_color = true;
            out->color = ws2812_rgb(r, g, b);
        }
    }

    if (json_find_n(payload, length, "color_temp", &value)) {
        int32_t number;

        if (json_get_int(&value, &number) && number > 0) {
            out->has_mireds = true;
            /* light_set_mireds() clamps; carrying the raw value here keeps
               this layer to translation. */
            out->mireds = (uint16_t)(number > 65535 ? 65535 : number);
        }
    }

    if (json_find_n(payload, length, "effect", &value)) {
        char name[40];

        if (json_get_string(&value, name, sizeof(name))) {
            light_effect_t effect;

            if (light_effect_from_name(name, &effect)) {
                out->has_effect = true;
                out->effect = effect;
            } else {
                /* Reported rather than ignored: it almost always means the
                   published effect list and this firmware disagree. */
                out->unknown_effect = true;
            }
        } else {
            out->unknown_effect = true;
        }
    }

    return true;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool ha_parse_range_command(const char *payload, size_t length, uint32_t *out)
{
    if (payload == NULL || length == 0u || out == NULL) {
        return false;
    }

    size_t i = 0u;
    while (i < length && is_space(payload[i])) {
        i++;
    }
    if (i < length && payload[i] == '+') {
        i++;
    }

    uint32_t value = 0u;
    bool has_digit = false;
    while (i < length && payload[i] >= '0' && payload[i] <= '9') {
        const uint32_t digit = (uint32_t)(payload[i] - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        has_digit = true;
        i++;
    }
    if (!has_digit) {
        return false;
    }

    /* Number entities can serialize an integral slider value as either 42 or
       42.0. Accept only zeroes after the decimal point: silently rounding a
       real fraction would make the state we publish disagree with its command. */
    if (i < length && payload[i] == '.') {
        i++;
        const size_t fraction = i;
        while (i < length && payload[i] == '0') {
            i++;
        }
        if (i == fraction) {
            return false;
        }
    }

    while (i < length && is_space(payload[i])) {
        i++;
    }
    if (i != length) {
        return false;
    }

    *out = value;
    return true;
}

void ha_apply_command(const ha_command_t *command, light_t *light, uint32_t now_ms)
{
    if (command == NULL || light == NULL) {
        return;
    }

    /*
     * Appearance before power. Home Assistant sends "turn on, and be red" as
     * one message; applying the power first would show the previous colour for
     * a frame before the new one landed.
     */
    if (command->has_brightness) {
        light_set_brightness(light, command->brightness, now_ms);
    }
    if (command->has_color) {
        light_set_color(light, command->color, now_ms);
    }
    if (command->has_mireds) {
        light_set_mireds(light, command->mireds, now_ms);
    }
    if (command->has_effect) {
        light_set_effect(light, command->effect, now_ms);
    }
    if (command->has_power) {
        light_set_power(light, command->power, now_ms);
    }
}
