#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "config_store.h"

/* ---------------------------------------------------------------------------
 * Keys
 *
 * These are a stored interface: renaming one silently loses whatever was
 * under the old name, so a board that has been configured would come back on
 * defaults after a firmware update with no indication why.
 * -------------------------------------------------------------------------*/

#define KEY_SSID "wifi_ssid"
#define KEY_PASSWORD "wifi_password"
#define KEY_HOSTNAME "wifi_hostname"
#define KEY_BROKER "mqtt_broker"
#define KEY_PORT "mqtt_port"
#define KEY_MQTT_USER "mqtt_user"
#define KEY_MQTT_PASS "mqtt_pass"
#define KEY_DEVICE_ID "device_id"
#define KEY_HA_PREFIX "ha_prefix"

#define KEY_LIGHT_ON "light_on"
#define KEY_LIGHT_BRIGHTNESS "light_bri"
#define KEY_LIGHT_RGB "light_rgb"
#define KEY_LIGHT_MIREDS "light_ct"
#define KEY_LIGHT_MODE "light_mode"
#define KEY_LIGHT_EFFECT "light_effect"

/* ---------------------------------------------------------------------------
 * Loading and storing
 * -------------------------------------------------------------------------*/

static void load_strings(app_t *app)
{
    config_store_t *store = persistent_config_store(&app->config);
    stored_settings_t *s = &app->stored;

    config_get_string(store, KEY_SSID, s->ssid, sizeof(s->ssid), "");
    config_get_string(store, KEY_PASSWORD, s->password, sizeof(s->password), "");
    config_get_string(store, KEY_HOSTNAME, s->hostname, sizeof(s->hostname), "home-led");
    config_get_string(store, KEY_BROKER, s->broker_host, sizeof(s->broker_host), "");
    config_get_string(store, KEY_PORT, s->broker_port, sizeof(s->broker_port), "1883");
    config_get_string(store, KEY_MQTT_USER, s->mqtt_user, sizeof(s->mqtt_user), "");
    config_get_string(store, KEY_MQTT_PASS, s->mqtt_pass, sizeof(s->mqtt_pass), "");
    config_get_string(store, KEY_DEVICE_ID, s->device_id, sizeof(s->device_id), "home-led");
    config_get_string(store, KEY_HA_PREFIX, s->ha_prefix, sizeof(s->ha_prefix),
                      "homeassistant");
}

/*
 * Restore the user's brightness and RGB preferences, then apply the fixed
 * startup policy: off, solid, and 3000 K colour-temperature mode. This makes
 * boot safe and predictable even when an earlier image saved an active
 * rainbow in flash.
 *
 * Every field goes through light_restore(), which validates them -- this data
 * comes out of flash, where a half-finished write or an older firmware can
 * leave anything, and an effect index past the end of the table would be read
 * straight out of the name array.
 */
static void load_light(app_t *app, uint32_t now_ms)
{
    config_store_t *store = persistent_config_store(&app->config);
    light_settings_t stored;
    uint32_t value;

    stored.on = false;

    config_get_u32(store, KEY_LIGHT_BRIGHTNESS, &value, LIGHT_DEFAULT_BRIGHTNESS);
    stored.brightness = (uint8_t)(value > 255u ? 255u : value);

    config_get_u32(store, KEY_LIGHT_RGB, &value, 0xFFFFFFu);
    stored.color = ws2812_rgb((uint8_t)(value >> 16), (uint8_t)(value >> 8),
                              (uint8_t)value);

    stored.mireds = LIGHT_DEFAULT_MIREDS;
    stored.color_mode = LIGHT_COLOR_MODE_TEMP;
    stored.effect = LIGHT_EFFECT_SOLID;

    light_restore(&app->light, &stored, now_ms);
}

void settings_load(app_t *app, uint32_t now_ms)
{
    persistent_config_load(&app->config, app->config_buffer, sizeof(app->config_buffer));
    load_strings(app);
    load_light(app, now_ms);
}

bool settings_store(app_t *app)
{
    config_store_t *store = persistent_config_store(&app->config);
    light_settings_t current;

    light_capture(&app->light, &current);

    const uint32_t rgb = ((uint32_t)current.color.r << 16) |
                         ((uint32_t)current.color.g << 8) | current.color.b;

    config_set_u32(store, KEY_LIGHT_ON, current.on ? 1u : 0u);
    config_set_u32(store, KEY_LIGHT_BRIGHTNESS, current.brightness);
    config_set_u32(store, KEY_LIGHT_RGB, rgb);
    config_set_u32(store, KEY_LIGHT_MIREDS, current.mireds);
    config_set_u32(store, KEY_LIGHT_MODE, (uint32_t)current.color_mode);
    config_set_u32(store, KEY_LIGHT_EFFECT, (uint32_t)current.effect);

    return persistent_config_save(&app->config) == PERSISTENT_CONFIG_OK;
}

/* ---------------------------------------------------------------------------
 * Console
 * -------------------------------------------------------------------------*/

/*
 * One shape for every "show it or set it" string setting. There are nine, and
 * writing nine near-identical commands is how they drift apart.
 *
 * `secret` prints a placeholder instead of the value, for the two that are
 * passwords. `did_set` reports whether anything was written: cli_rest()
 * consumes the remainder of the line, so it can only be asked once and the
 * caller cannot look for itself.
 */
static int setting_command(cli_t *c, const char *label, const char *key, char *value,
                           size_t size, bool secret, app_t *app, bool *did_set)
{
    const char *given = cli_rest(c);

    if (did_set != NULL) {
        *did_set = false;
    }

    if (given == NULL) {
        if (secret) {
            cli_printf(c, "%s %s\r\n", label, value[0] != '\0' ? "<set>" : "<unset>");
        } else {
            cli_printf(c, "%s %s\r\n", label, value[0] != '\0' ? value : "<unset>");
        }
        return CLI_OK;
    }

    if (strlen(given) >= size) {
        cli_printf(c, "too long, %u characters at most\r\n", (unsigned)(size - 1u));
        return CLI_ERR_ARG;
    }

    snprintf(value, size, "%s", given);
    (void)config_set_string(persistent_config_store(&app->config), key, value);
    if (did_set != NULL) {
        *did_set = true;
    }
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

#define SETTING_COMMAND(name, label, key, field, secret)                        \
    static int name(cli_t *c, void *user_data)                                  \
    {                                                                           \
        app_t *app = (app_t *)user_data;                                        \
        return setting_command(c, label, key, app->stored.field,                \
                               sizeof(app->stored.field), secret, app, NULL);   \
    }

SETTING_COMMAND(cmd_ssid, "ssid", KEY_SSID, ssid, false)
SETTING_COMMAND(cmd_password, "password", KEY_PASSWORD, password, true)
SETTING_COMMAND(cmd_hostname, "hostname", KEY_HOSTNAME, hostname, false)
SETTING_COMMAND(cmd_broker, "broker", KEY_BROKER, broker_host, false)
SETTING_COMMAND(cmd_port, "port", KEY_PORT, broker_port, false)
SETTING_COMMAND(cmd_mqttuser, "mqttuser", KEY_MQTT_USER, mqtt_user, false)
SETTING_COMMAND(cmd_mqttpass, "mqttpass", KEY_MQTT_PASS, mqtt_pass, true)
SETTING_COMMAND(cmd_haprefix, "haprefix", KEY_HA_PREFIX, ha_prefix, false)

/* Not the macro: the topics were derived at startup from the old id, so a
   change needs a word about when it takes effect. */
static int cmd_deviceid(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    bool changed = false;

    const int result = setting_command(c, "deviceid", KEY_DEVICE_ID, app->stored.device_id,
                                       sizeof(app->stored.device_id), false, app, &changed);

    if (changed) {
        cli_write(c, "reboot to republish under the new id\r\n");
    }
    return result;
}

static int cmd_save(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    if (!settings_store(app)) {
        cli_write(c, "save failed\r\n");
        return CLI_ERR_FAILED;
    }
    app->saved_generation = app->light.generation;
    cli_write(c, "ok\r\n");
    return CLI_OK;
}

size_t settings_commands(app_t *app, cli_command_t *out, size_t capacity)
{
    static const cli_command_t table[SETTINGS_COMMAND_COUNT] = {
        { "ssid",     "ssid [name] - show or set",            cmd_ssid,     NULL },
        { "password", "password [value] - set only",          cmd_password, NULL },
        { "hostname", "hostname [name] - show or set",        cmd_hostname, NULL },
        { "broker",   "broker [host] - show or set",          cmd_broker,   NULL },
        { "port",     "port [n] - show or set",               cmd_port,     NULL },
        { "mqttuser", "mqttuser [name] - show or set",        cmd_mqttuser, NULL },
        { "mqttpass", "mqttpass [value] - set only",          cmd_mqttpass, NULL },
        { "deviceid", "deviceid [id] - show or set",          cmd_deviceid, NULL },
        { "haprefix", "haprefix [prefix] - discovery prefix", cmd_haprefix, NULL },
        { "save",     "keep every setting in flash",          cmd_save,     NULL },
    };

    size_t written = 0;

    for (size_t i = 0; i < SETTINGS_COMMAND_COUNT && written < capacity; i++) {
        out[written] = table[i];
        /* The app is every command's user_data, which is how they reach the
           store without a static. */
        out[written].user_data = app;
        written++;
    }
    return written;
}
