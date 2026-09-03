/*
 * home_led - a WS2812 strip as a Home Assistant light.
 *
 * The only file here that touches the Pico SDK. The light's behaviour lives in
 * light.c, what reaches the strip in effects.c, and the Home Assistant schema
 * in ha.c -- all three free of the SDK, and all three covered by host tests.
 * What is left is wiring: read settings out of flash, poll the radio, render a
 * frame, and move payloads between the broker and ha.c.
 *
 * Nothing is compiled in. WiFi credentials, the broker, the device id and the
 * light's own last settings all live in flash and are typed in once over the
 * console (DESIGN_DOC.md section 13). A deployed board needs no console after
 * that: it associates, connects, announces itself and comes back the colour it
 * was.
 *
 * See README.md for the console session that sets one up.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "effects.h"
#include "ha.h"
#include "json.h"
#include "light.h"
#include "mqtt.h"
#include "persistent_config.h"
#include "wifi.h"
#include "ws2812.h"

#if WIFI_SUPPORTED
#include "pico/cyw43_arch.h"
#endif

/* ---------------------------------------------------------------------------
 * Timing
 * -------------------------------------------------------------------------*/

/*
 * 50 frames a second. 300 pixels occupy the wire for about 9 ms, so this is
 * comfortably clear of the strip's own limit and leaves the rest of the loop
 * -- mostly lwIP -- the majority of every period.
 */
#define FRAME_INTERVAL_MS 20u

/*
 * How long the light must sit unchanged before its settings are written to
 * flash.
 *
 * Home Assistant sends a message per step while a slider is dragged. Writing
 * each one would be dozens of erase/program cycles for one gesture, so the
 * write waits until the gesture is over. Losing the last few seconds of
 * fiddling to a power cut is a much smaller problem than wearing the sector
 * out.
 */
#define SAVE_QUIET_MS 10000u

/* And a floor on how often the state is published, for the same reason
   applied to the broker rather than to flash. */
#define PUBLISH_INTERVAL_MS 250u

/* The onboard LED blinks slowly once there is a broker session, and quickly
   while there is not, so a board with no console still says where it got to. */
#define HEARTBEAT_LINKED_MS 1000u
#define HEARTBEAT_SEARCHING_MS 150u

/* ---------------------------------------------------------------------------
 * Stored settings
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

static uint8_t config_buffer[1024];
static persistent_config_t settings;

/* Borrowed by wifi_config_t and mqtt_config_t rather than copied, so these
   must outlive the connections they configure. */
static char ssid[WIFI_SSID_MAX_LENGTH + 1];
static char password[WIFI_PASSWORD_MAX_LENGTH + 1];
static char hostname[33];
static char broker_host[128];
static char broker_port_text[8];
static char mqtt_user[64];
static char mqtt_pass[64];
static char device_id[HA_DEVICE_ID_MAX_LENGTH + 1];
static char ha_prefix[32];

/* ---------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------*/

static cli_t cli;
static char line_buffer[192];
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 24u];

static wifi_t wifi;
static mqtt_t mqtt;
static ha_t ha;

static light_t light;
static effects_t effects;

static ws2812_strip_t strip;
static ws2812_color_t pixels[APP_LED_COUNT];
static uint32_t wire_buffer[APP_LED_COUNT];

static bool strip_ready;
static bool radio_ready;
static bool show_test_pattern;

/* Change tracking: what the broker and the flash have each last been told,
   and when the light last actually moved. */
static uint32_t published_generation;
static uint32_t saved_generation;
static uint32_t seen_generation;
static uint32_t last_change_ms;
static uint32_t last_publish_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

/* ---------------------------------------------------------------------------
 * Settings
 * -------------------------------------------------------------------------*/

static void load_settings(void)
{
    config_store_t *store = persistent_config_store(&settings);

    config_get_string(store, KEY_SSID, ssid, sizeof(ssid), "");
    config_get_string(store, KEY_PASSWORD, password, sizeof(password), "");
    config_get_string(store, KEY_HOSTNAME, hostname, sizeof(hostname), "home-led");
    config_get_string(store, KEY_BROKER, broker_host, sizeof(broker_host), "");
    config_get_string(store, KEY_PORT, broker_port_text, sizeof(broker_port_text), "1883");
    config_get_string(store, KEY_MQTT_USER, mqtt_user, sizeof(mqtt_user), "");
    config_get_string(store, KEY_MQTT_PASS, mqtt_pass, sizeof(mqtt_pass), "");
    config_get_string(store, KEY_DEVICE_ID, device_id, sizeof(device_id), "home-led");
    config_get_string(store, KEY_HA_PREFIX, ha_prefix, sizeof(ha_prefix), "homeassistant");
}

/* Restore the light itself, so a board that lost power comes back the way it
   was rather than at some default nobody chose. light_restore() validates
   every field, which matters because this data comes out of flash. */
static void load_light(uint32_t at_ms)
{
    config_store_t *store = persistent_config_store(&settings);
    light_settings_t stored;
    uint32_t value;

    config_get_u32(store, KEY_LIGHT_ON, &value, 1u);
    stored.on = (value != 0u);

    config_get_u32(store, KEY_LIGHT_BRIGHTNESS, &value, 128u);
    stored.brightness = (uint8_t)(value > 255u ? 255u : value);

    config_get_u32(store, KEY_LIGHT_RGB, &value, 0xFFFFFFu);
    stored.color = ws2812_rgb((uint8_t)(value >> 16), (uint8_t)(value >> 8),
                              (uint8_t)value);

    config_get_u32(store, KEY_LIGHT_MIREDS, &value, 250u);
    stored.mireds = (uint16_t)(value > 65535u ? 65535u : value);

    config_get_u32(store, KEY_LIGHT_MODE, &value, (uint32_t)LIGHT_COLOR_MODE_TEMP);
    stored.color_mode = (light_color_mode_t)value;

    config_get_u32(store, KEY_LIGHT_EFFECT, &value, (uint32_t)LIGHT_EFFECT_SOLID);
    stored.effect = (light_effect_t)value;

    light_restore(&light, &stored, at_ms);
}

static bool store_light(void)
{
    config_store_t *store = persistent_config_store(&settings);
    light_settings_t current;

    light_capture(&light, &current);

    const uint32_t rgb = ((uint32_t)current.color.r << 16) |
                         ((uint32_t)current.color.g << 8) | current.color.b;

    config_set_u32(store, KEY_LIGHT_ON, current.on ? 1u : 0u);
    config_set_u32(store, KEY_LIGHT_BRIGHTNESS, current.brightness);
    config_set_u32(store, KEY_LIGHT_RGB, rgb);
    config_set_u32(store, KEY_LIGHT_MIREDS, current.mireds);
    config_set_u32(store, KEY_LIGHT_MODE, (uint32_t)current.color_mode);
    config_set_u32(store, KEY_LIGHT_EFFECT, (uint32_t)current.effect);

    return persistent_config_save(&settings) == PERSISTENT_CONFIG_OK;
}

static wifi_config_t current_wifi_config(void)
{
    return (wifi_config_t){
        .ssid = ssid,
        .password = password,
        .hostname = hostname,
        .attempt_timeout_ms = WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS,
        .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
    };
}

/* ---------------------------------------------------------------------------
 * MQTT
 * -------------------------------------------------------------------------*/

static void publish_state(void)
{
    static char payload[HA_STATE_BUFFER_SIZE];

    if (!mqtt_is_connected(&mqtt)) {
        return;
    }

    const size_t length = ha_build_state(&ha, &light, payload, sizeof(payload));
    if (length == 0) {
        return;
    }

    /* Retained, so Home Assistant knows what the light is doing as soon as it
       subscribes rather than having to wait for the next change. */
    (void)mqtt_publish_message(&mqtt, ha.topic_state, payload, (uint16_t)length, 1, true);
    published_generation = light.generation;
    last_publish_ms = now_ms();
}

static void publish_discovery(void)
{
    static char payload[HA_DISCOVERY_BUFFER_SIZE];

    const size_t length = ha_build_discovery(&ha, payload, sizeof(payload));
    if (length == 0) {
        cli_write(&cli, "\r\n[ha] the discovery document did not fit\r\n");
        return;
    }

    (void)mqtt_publish_message(&mqtt, ha.topic_config, payload, (uint16_t)length, 1, true);
}

/*
 * Everything that has to be re-established on a new broker session.
 *
 * lwIP always connects with the clean-session flag set, so the subscription is
 * gone after any reconnect and a restarted Home Assistant only sees what is
 * retained. Doing all of this from mqtt's on_connect means it happens on the
 * first connection and every later one, with no separate bookkeeping.
 */
static void on_broker_session(void *arg)
{
    (void)arg;

    (void)mqtt_subscribe_topic(&mqtt, ha.topic_command, 1);
    publish_discovery();
    (void)mqtt_publish_message(&mqtt, ha.topic_availability, HA_AVAILABLE,
                               (uint16_t)strlen(HA_AVAILABLE), 1, true);
    publish_state();

    cli_printf(&cli, "\r\n[ha] announced as %s (session %lu)\r\n", ha.device_id,
               (unsigned long)mqtt_sessions(&mqtt));
    cli_write_prompt(&cli);
}

static void on_broker_message(void *arg, const char *topic, const uint8_t *payload,
                              size_t length)
{
    (void)arg;

    if (strcmp(topic, ha.topic_command) != 0) {
        return;
    }

    ha_command_t command;
    if (!ha_parse_command((const char *)payload, length, &command)) {
        cli_printf(&cli, "\r\n[ha] unreadable command (%u bytes)\r\n", (unsigned)length);
        cli_write_prompt(&cli);
        return;
    }

    if (command.unknown_effect) {
        /* Almost always means the published effect list and this firmware have
           drifted apart, which is worth saying out loud. */
        cli_write(&cli, "\r\n[ha] command named an effect this firmware does not have\r\n");
        cli_write_prompt(&cli);
    }

    ha_apply_command(&command, &light, now_ms());
}

static mqtt_config_t current_mqtt_config(void)
{
    return (mqtt_config_t){
        .broker_host = broker_host,
        .broker_port = (uint16_t)atoi(broker_port_text),
        .client_id = device_id,
        .username = mqtt_user,
        .password = mqtt_pass,
        .keep_alive_s = MQTT_DEFAULT_KEEP_ALIVE_S,

        /* The broker says "offline" on this device's behalf if it disappears
           without a clean disconnect, which is the only way Home Assistant
           learns about a power cut. */
        .will_topic = ha.topic_availability,
        .will_message = HA_UNAVAILABLE,
        .will_qos = 1,
        .will_retain = true,

        .on_message = on_broker_message,
        .on_connect = on_broker_session,
        .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
    };
}

/* ---------------------------------------------------------------------------
 * Console: the light
 * -------------------------------------------------------------------------*/

static int cmd_on(cli_t *c, void *user_data)
{
    (void)user_data;
    light_set_power(&light, true, now_ms());
    cli_write(c, "on\r\n");
    return CLI_OK;
}

static int cmd_off(cli_t *c, void *user_data)
{
    (void)user_data;
    light_set_power(&light, false, now_ms());
    cli_write(c, "off\r\n");
    return CLI_OK;
}

static int cmd_brightness(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;

    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "brightness %u (showing %u)\r\n", (unsigned)light.brightness,
                   (unsigned)light_current_brightness(&light));
        return CLI_OK;
    }
    if (value > 255u) {
        cli_write(c, "0..255\r\n");
        return CLI_ERR_ARG;
    }

    light_set_brightness(&light, (uint8_t)value, now_ms());
    cli_printf(c, "brightness %u\r\n", (unsigned)value);
    return CLI_OK;
}

static int cmd_rgb(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (!cli_next_u32(c, &r) || !cli_next_u32(c, &g) || !cli_next_u32(c, &b)) {
        cli_printf(c, "usage: rgb <r> <g> <b>  (now %u %u %u)\r\n",
                   (unsigned)light.color.r, (unsigned)light.color.g,
                   (unsigned)light.color.b);
        return CLI_ERR_ARG;
    }
    if (r > 255u || g > 255u || b > 255u) {
        cli_write(c, "each channel is 0..255\r\n");
        return CLI_ERR_ARG;
    }

    light_set_color(&light, ws2812_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b), now_ms());
    cli_printf(c, "rgb %u %u %u\r\n", (unsigned)r, (unsigned)g, (unsigned)b);
    return CLI_OK;
}

static int cmd_ct(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;

    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "color temperature %u mireds (showing %u)\r\n",
                   (unsigned)light.mireds, (unsigned)light_current_mireds(&light));
        return CLI_OK;
    }

    light_set_mireds(&light, (uint16_t)(value > 65535u ? 65535u : value), now_ms());
    cli_printf(c, "color temperature %u mireds\r\n", (unsigned)light.mireds);
    return CLI_OK;
}

static int cmd_effect(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *name = cli_next_token(c);

    if (name == NULL) {
        cli_printf(c, "effect %s\r\n", light_effect_name(light.effect));
        for (unsigned i = 0; i < (unsigned)LIGHT_EFFECT_COUNT; i++) {
            cli_printf(c, "  %s\r\n", light_effect_name((light_effect_t)i));
        }
        return CLI_OK;
    }

    light_effect_t effect;
    if (!light_effect_from_name(name, &effect)) {
        cli_printf(c, "no such effect: %s\r\n", name);
        return CLI_ERR_ARG;
    }

    light_set_effect(&light, effect, now_ms());
    cli_printf(c, "effect %s\r\n", light_effect_name(effect));
    return CLI_OK;
}

static int cmd_test(cli_t *c, void *user_data)
{
    (void)user_data;

    /* Overrides the effect until switched off again, so wiring can be checked
       without disturbing whatever the light was set to. */
    show_test_pattern = !show_test_pattern;
    cli_printf(c, "test pattern %s\r\n", show_test_pattern ? "on" : "off");
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;

    cli_printf(c, "strip        %u leds on gpio %u%s\r\n", (unsigned)APP_LED_COUNT,
               (unsigned)APP_LED_PIN, strip_ready ? "" : "  (NOT INITIALISED)");
    cli_printf(c, "light        %s, effect %s\r\n", light.on ? "on" : "off",
               light_effect_name(light.effect));
    cli_printf(c, "brightness   %u (showing %u)\r\n", (unsigned)light.brightness,
               (unsigned)light_current_brightness(&light));
    if (light.color_mode == LIGHT_COLOR_MODE_TEMP) {
        cli_printf(c, "colour       %u mireds (showing %u)\r\n", (unsigned)light.mireds,
                   (unsigned)light_current_mireds(&light));
    } else {
        cli_printf(c, "colour       rgb %u %u %u\r\n", (unsigned)light.color.r,
                   (unsigned)light.color.g, (unsigned)light.color.b);
    }
    cli_printf(c, "fading       %s\r\n", light_is_fading(&light) ? "yes" : "no");
    cli_printf(c, "wifi         %s", wifi_state_name(wifi_state(&wifi)));
    if (wifi_is_connected(&wifi)) {
        cli_printf(c, " as %s", wifi_address(&wifi));
    }
    cli_printf(c, "\r\nbroker       %s:%s, %s\r\n",
               broker_host[0] != '\0' ? broker_host : "<unset>", broker_port_text,
               mqtt_state_name(mqtt_state(&mqtt)));
    cli_printf(c, "device id    %s\r\n", device_id);
    cli_printf(c, "topics       %s\r\n             %s\r\n", ha.topic_command,
               ha.topic_state);
    cli_printf(c, "sessions     %lu, dropped %lu\r\n",
               (unsigned long)mqtt_sessions(&mqtt),
               (unsigned long)mqtt_messages_dropped(&mqtt));
    cli_printf(c, "unsaved      %s\r\n",
               light.generation != saved_generation ? "yes" : "no");
    return CLI_OK;
}

/* ---------------------------------------------------------------------------
 * Console: settings
 * -------------------------------------------------------------------------*/

/* One shape for every "show it or set it" string setting, since there are nine
   of them and writing nine near-identical commands is how they drift apart. */
static int setting_command(cli_t *c, const char *label, const char *key, char *value,
                           size_t size, bool secret, bool *did_set)
{
    /* cli_rest() consumes the remainder of the line, so it can only be asked
       once -- which is why the caller is told what happened rather than
       looking for itself. */
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
    (void)config_set_string(persistent_config_store(&settings), key, value);
    if (did_set != NULL) {
        *did_set = true;
    }
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_ssid(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "ssid", KEY_SSID, ssid, sizeof(ssid), false, NULL);
}

static int cmd_password(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "password", KEY_PASSWORD, password, sizeof(password), true, NULL);
}

static int cmd_hostname(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "hostname", KEY_HOSTNAME, hostname, sizeof(hostname), false, NULL);
}

static int cmd_broker(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "broker", KEY_BROKER, broker_host, sizeof(broker_host), false, NULL);
}

static int cmd_port(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "port", KEY_PORT, broker_port_text,
                           sizeof(broker_port_text), false, NULL);
}

static int cmd_mqttuser(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "mqttuser", KEY_MQTT_USER, mqtt_user, sizeof(mqtt_user), false, NULL);
}

static int cmd_mqttpass(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "mqttpass", KEY_MQTT_PASS, mqtt_pass, sizeof(mqtt_pass), true, NULL);
}

static int cmd_deviceid(cli_t *c, void *d)
{
    (void)d;
    bool changed = false;
    const int result = setting_command(c, "deviceid", KEY_DEVICE_ID, device_id,
                                       sizeof(device_id), false, &changed);

    if (changed) {
        /* The topics were derived at startup, so they still name the old id
           until the next boot. */
        cli_write(c, "reboot to republish under the new id\r\n");
    }
    return result;
}

static int cmd_haprefix(cli_t *c, void *d)
{
    (void)d;
    return setting_command(c, "haprefix", KEY_HA_PREFIX, ha_prefix, sizeof(ha_prefix), false, NULL);
}

static int cmd_save(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!store_light()) {
        cli_write(c, "save failed\r\n");
        return CLI_ERR_FAILED;
    }
    saved_generation = light.generation;
    cli_write(c, "ok\r\n");
    return CLI_OK;
}

/* ---------------------------------------------------------------------------
 * Console: network
 * -------------------------------------------------------------------------*/

static int cmd_connect(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!WIFI_SUPPORTED) {
        cli_write(c, "this board has no radio\r\n");
        return CLI_ERR_STATE;
    }
    if (wifi_check_credentials(ssid, password) != WIFI_CREDENTIALS_OK) {
        cli_write(c, "set ssid and password first\r\n");
        return CLI_ERR_STATE;
    }

    const wifi_config_t config = current_wifi_config();
    const wifi_result_t result = wifi_connect(&wifi, &config);
    if (result != WIFI_OK) {
        cli_printf(c, "error: %s\r\n", wifi_result_name(result));
        return CLI_ERR_FAILED;
    }

    if (broker_host[0] != '\0') {
        const mqtt_config_t mqtt_config = current_mqtt_config();
        (void)mqtt_connect(&mqtt, &mqtt_config);
    }
    cli_printf(c, "associating with %s\r\n", ssid);
    return CLI_OK;
}

static int cmd_disconnect(cli_t *c, void *user_data)
{
    (void)user_data;

    mqtt_close(&mqtt);
    wifi_disconnect(&wifi);
    cli_write(c, "disconnected\r\n");
    return CLI_OK;
}

static int cmd_announce(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!mqtt_is_connected(&mqtt)) {
        cli_write(c, "not connected to a broker\r\n");
        return CLI_ERR_STATE;
    }

    /* The same thing a new session does, for when Home Assistant has been
       reinstalled and its retained copy is gone. */
    on_broker_session(NULL);
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "on",         "switch the light on",                     cmd_on,         NULL },
    { "off",        "switch the light off",                    cmd_off,        NULL },
    { "bri",        "bri [0-255] - brightness, show or set",   cmd_brightness, NULL },
    { "rgb",        "rgb <r> <g> <b> - set an rgb colour",      cmd_rgb,        NULL },
    { "ct",         "ct [mireds] - colour temperature",        cmd_ct,         NULL },
    { "effect",     "effect [name] - show, list, or select",   cmd_effect,     NULL },
    { "test",       "toggle the wiring test pattern",          cmd_test,       NULL },
    { "status",     "light, strip, wifi and broker state",     cmd_status,     NULL },

    { "ssid",       "ssid [name] - show or set",               cmd_ssid,       NULL },
    { "password",   "password [value] - set only",             cmd_password,   NULL },
    { "hostname",   "hostname [name] - show or set",           cmd_hostname,   NULL },
    { "broker",     "broker [host] - show or set",             cmd_broker,     NULL },
    { "port",       "port [n] - show or set",                  cmd_port,       NULL },
    { "mqttuser",   "mqttuser [name] - show or set",           cmd_mqttuser,   NULL },
    { "mqttpass",   "mqttpass [value] - set only",             cmd_mqttpass,   NULL },
    { "deviceid",   "deviceid [id] - show or set",             cmd_deviceid,   NULL },
    { "haprefix",   "haprefix [prefix] - discovery prefix",    cmd_haprefix,   NULL },
    { "save",       "keep every setting in flash",             cmd_save,       NULL },

    { "connect",    "associate and connect to the broker",     cmd_connect,    NULL },
    { "disconnect", "stop trying",                             cmd_disconnect, NULL },
    { "announce",   "republish the discovery document",        cmd_announce,   NULL },
};

/* ---------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------*/

static void render_frame(uint32_t at_ms)
{
    if (!strip_ready || ws2812_is_busy(&strip)) {
        /* The previous frame is still going out, or its latch gap has not
           elapsed. Skipping is the right answer for an animation -- the next
           frame is 20 ms away and will be more current than this one. */
        return;
    }

    if (show_test_pattern) {
        effects_render_test_pattern(pixels, APP_LED_COUNT);
    } else {
        effects_render(&effects, &light, pixels, APP_LED_COUNT, at_ms);
    }

    (void)ws2812_show_async(&strip);
}

static void heartbeat(uint32_t at_ms)
{
#if WIFI_SUPPORTED
    static uint32_t last_ms;
    static bool lit;

    /* The LED hangs off the CYW43, so it is only reachable once wifi_init()
       has brought the chip up. Poking it after a failed init would be a call
       into a driver that was never started. */
    if (!radio_ready) {
        return;
    }

    const uint32_t interval =
        mqtt_is_connected(&mqtt) ? HEARTBEAT_LINKED_MS : HEARTBEAT_SEARCHING_MS;

    if (at_ms - last_ms < interval) {
        return;
    }
    last_ms = at_ms;
    lit = !lit;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, lit);
#else
    (void)at_ms;
#endif
}

/* ---------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------*/

static void start_strip(void)
{
    const ws2812_config_t config = {
        .pio = pio0,
        .pin = APP_LED_PIN,
        .pixels = pixels,
        .length = APP_LED_COUNT,
        .is_rgbw = false,
        .frequency_hz = WS2812_DEFAULT_FREQUENCY_HZ,

        /* A wire buffer is what enables DMA, and at 300 pixels the difference
           is 9 ms of the processor's time per frame -- which is the whole
           budget lwIP has to work in. */
        .wire_buffer = wire_buffer,
    };

    const ws2812_result_t result = ws2812_init(&strip, &config);
    if (result != WS2812_OK) {
        return;
    }

    /*
     * Gamma at the driver, brightness at the effects.
     *
     * Gamma has to be the last thing applied, after any scaling, or it is
     * being corrected and then re-linearised. Brightness stays at 255 here
     * because the effects apply it themselves with dithering, which is a
     * scaling decision and has to be made per pixel.
     */
    ws2812_set_brightness(&strip, 255);
    ws2812_set_gamma(&strip, ws2812_gamma_table);
    strip_ready = true;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const uint32_t started_ms = now_ms();

    persistent_config_load(&settings, config_buffer, sizeof(config_buffer));
    load_settings();
    load_light(started_ms);
    saved_generation = light.generation;
    published_generation = light.generation;
    seen_generation = light.generation;
    last_change_ms = started_ms;

    effects_init(&effects, started_ms);
    start_strip();

    const wifi_result_t wifi_started = wifi_init(&wifi);
    radio_ready = (wifi_started == WIFI_OK);
    mqtt_init(&mqtt);

    const bool ha_ready = ha_init(&ha, device_id, ha_prefix);

    size_t count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0; i < count_of(own_commands) && count < count_of(commands); i++) {
        commands[count++] = own_commands[i];
    }

    const cli_config_t cli_config = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "led> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nhome_led  board %s  %u leds on gpio %u  radio %s\r\n",
               PICO_BOARD, (unsigned)APP_LED_COUNT, (unsigned)APP_LED_PIN,
               WIFI_SUPPORTED ? "present" : "none");
    if (!strip_ready) {
        cli_write(&cli, "the strip did not initialise; check the pio and pin\r\n");
    }
    if (wifi_started != WIFI_OK) {
        cli_printf(&cli, "wifi_init: %s\r\n", wifi_result_name(wifi_started));
    }
    if (!ha_ready) {
        cli_write(&cli, "the device id is unusable; set deviceid and reboot\r\n");
    }
    cli_write(&cli, "type help. Set ssid/password/broker, save, then connect\r\n");
    cli_write_prompt(&cli);

    /* Come up on their own when the settings are already there, so a deployed
       board needs no console after the first setup. */
    if (wifi_started == WIFI_OK && ha_ready &&
        wifi_check_credentials(ssid, password) == WIFI_CREDENTIALS_OK) {
        const wifi_config_t config = current_wifi_config();

        if (wifi_connect(&wifi, &config) == WIFI_OK && broker_host[0] != '\0') {
            const mqtt_config_t mqtt_config = current_mqtt_config();
            (void)mqtt_connect(&mqtt, &mqtt_config);
        }
    }

    uint32_t last_frame_ms = started_ms;
    wifi_state_t reported_wifi = WIFI_STATE_IDLE;
    mqtt_state_t reported_mqtt = MQTT_STATE_IDLE;

    while (true) {
        const uint32_t at_ms = now_ms();

        cli_poll(&cli);
        wifi_poll(&wifi);
        mqtt_poll(&mqtt);
        light_tick(&light, at_ms);

        if (at_ms - last_frame_ms >= FRAME_INTERVAL_MS) {
            last_frame_ms = at_ms;
            render_frame(at_ms);
        }

        /*
         * Note when the light last changed, so the flash write can wait for
         * the fiddling to stop.
         *
         * Driven by the generation counter itself rather than by what has been
         * published: publish_state() does nothing while there is no broker
         * session, so keying this off it would restart the timer on every
         * loop and a board running from the console alone would never save
         * anything at all.
         */
        if (light.generation != seen_generation) {
            seen_generation = light.generation;
            last_change_ms = at_ms;
        }

        if (light.generation != published_generation &&
            at_ms - last_publish_ms >= PUBLISH_INTERVAL_MS) {
            publish_state();
        }

        if (light.generation != saved_generation &&
            at_ms - last_change_ms >= SAVE_QUIET_MS) {
            if (store_light()) {
                saved_generation = light.generation;
            } else {
                /* Try again after another quiet period rather than spinning on
                   a failing flash write every loop. */
                last_change_ms = at_ms;
            }
        }

        const wifi_state_t wifi_now = wifi_state(&wifi);
        if (wifi_now != reported_wifi) {
            reported_wifi = wifi_now;
            cli_printf(&cli, "\r\n[wifi] %s", wifi_state_name(wifi_now));
            if (wifi_now == WIFI_STATE_CONNECTED) {
                cli_printf(&cli, " as %s", wifi_address(&wifi));
            }
            cli_write(&cli, "\r\n");
            cli_write_prompt(&cli);
        }

        const mqtt_state_t mqtt_now = mqtt_state(&mqtt);
        if (mqtt_now != reported_mqtt) {
            reported_mqtt = mqtt_now;
            cli_printf(&cli, "\r\n[mqtt] %s\r\n", mqtt_state_name(mqtt_now));
            cli_write_prompt(&cli);
        }

        heartbeat(at_ms);
    }
}
