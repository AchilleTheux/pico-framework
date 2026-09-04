#include "net.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/

static wifi_config_t wifi_config_of(const app_t *app)
{
    return (wifi_config_t){
        /* Borrowed, not copied, which is why these live in app->stored rather
           than on a stack somewhere. */
        .ssid = app->stored.ssid,
        .password = app->stored.password,
        .hostname = app->stored.hostname,
        .attempt_timeout_ms = WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS,
        .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
    };
}

static void on_broker_session(void *arg);
static void on_broker_message(void *arg, const char *topic, const uint8_t *payload,
                              size_t length);

static mqtt_config_t mqtt_config_of(app_t *app)
{
    return (mqtt_config_t){
        .broker_host = app->stored.broker_host,
        .broker_port = (uint16_t)atoi(app->stored.broker_port),
        .client_id = app->stored.device_id,
        .username = app->stored.mqtt_user,
        .password = app->stored.mqtt_pass,
        .keep_alive_s = MQTT_DEFAULT_KEEP_ALIVE_S,

        /* The broker says "offline" on this device's behalf if it disappears
           without a clean disconnect, which is the only way Home Assistant
           learns about a power cut. */
        .will_topic = app->ha.topic_availability,
        .will_message = HA_UNAVAILABLE,
        .will_qos = 1,
        .will_retain = true,

        .on_message = on_broker_message,
        .on_message_arg = app,
        .on_connect = on_broker_session,
        .on_connect_arg = app,

        .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
    };
}

/* ---------------------------------------------------------------------------
 * Publishing
 * -------------------------------------------------------------------------*/

bool net_publish_state(app_t *app)
{
    static char payload[HA_STATE_BUFFER_SIZE];
    static char number[HA_NUMBER_STATE_BUFFER_SIZE];

    if (!mqtt_is_connected(&app->mqtt)) {
        return false;
    }

    const size_t length = ha_build_state(&app->ha, &app->light, payload, sizeof(payload));
    if (length == 0) {
        return false;
    }

    /* Retained, so Home Assistant knows what the light is doing as soon as it
       subscribes rather than having to wait for the next change. */
    if (mqtt_publish_message(&app->mqtt, app->ha.topic_state, payload,
                             (uint16_t)length, 1, true) != MQTT_OK) {
        return false;
    }

    size_t number_length = ha_build_range_state(&app->range, HA_RANGE_FIRST,
                                                number, sizeof(number));
    if (number_length != 0u) {
        if (mqtt_publish_message(&app->mqtt, app->ha.topic_range_first_state,
                                 number, (uint16_t)number_length, 1, true) != MQTT_OK) {
            return false;
        }
    } else {
        return false;
    }
    number_length = ha_build_range_state(&app->range, HA_RANGE_LAST,
                                         number, sizeof(number));
    if (number_length != 0u) {
        if (mqtt_publish_message(&app->mqtt, app->ha.topic_range_last_state,
                                 number, (uint16_t)number_length, 1, true) != MQTT_OK) {
            return false;
        }
    } else {
        return false;
    }

    app->published_generation = app->light.generation;
    app->published_range_generation = app->range.generation;
    app->last_publish_ms = app_now_ms();
    return true;
}

typedef enum {
    DISCOVERY_LIGHT = 0,
    DISCOVERY_RANGE_FIRST,
    DISCOVERY_RANGE_LAST,
} discovery_entity_t;

static bool publish_discovery(app_t *app, discovery_entity_t entity)
{
    static char payload[HA_DISCOVERY_BUFFER_SIZE];

    const bool light = entity == DISCOVERY_LIGHT;
    const ha_range_endpoint_t endpoint = entity == DISCOVERY_RANGE_FIRST
        ? HA_RANGE_FIRST : HA_RANGE_LAST;
    const size_t length = light
        ? ha_build_discovery(&app->ha, payload, sizeof(payload))
        : ha_build_range_discovery(&app->ha, endpoint, APP_LED_COUNT,
                                   payload, sizeof(payload));
    if (length == 0) {
        cli_write(&app->cli, "\r\n[ha] the discovery document did not fit\r\n");
        return false;
    }

    const char *topic = light ? app->ha.topic_config
        : endpoint == HA_RANGE_FIRST ? app->ha.topic_range_first_config
                                     : app->ha.topic_range_last_config;
    return mqtt_publish_message(&app->mqtt, topic, payload,
                                (uint16_t)length, 1, true) == MQTT_OK;
}

void net_announce(app_t *app)
{
    (void)mqtt_subscribe_topic(&app->mqtt, app->ha.topic_command, 1);
    (void)mqtt_subscribe_topic(&app->mqtt, app->ha.topic_range_first_command, 1);
    (void)mqtt_subscribe_topic(&app->mqtt, app->ha.topic_range_last_command, 1);

    /* The three retained discovery documents do not fit in lwIP's output ring
       at once. net_poll() queues one at a time and retries a step until the
       previous packet has drained rather than silently losing an entity. */
    app->announce_step = 0u;
    app->announce_pending = true;
}

static void poll_announcement(app_t *app)
{
    if (!app->announce_pending || !mqtt_is_connected(&app->mqtt)) {
        return;
    }

    bool sent = false;
    switch (app->announce_step) {
        case 0u:
            sent = publish_discovery(app, DISCOVERY_LIGHT);
            break;
        case 1u:
            sent = publish_discovery(app, DISCOVERY_RANGE_FIRST);
            break;
        case 2u:
            sent = publish_discovery(app, DISCOVERY_RANGE_LAST);
            break;
        case 3u:
            sent = mqtt_publish_message(&app->mqtt, app->ha.topic_availability,
                                        HA_AVAILABLE, (uint16_t)strlen(HA_AVAILABLE),
                                        1, true) == MQTT_OK;
            break;
        default:
            if (net_publish_state(app)) {
                app->announce_pending = false;
                cli_printf(&app->cli, "\r\n[ha] announced as %s (session %lu)\r\n",
                           app->ha.device_id,
                           (unsigned long)mqtt_sessions(&app->mqtt));
                cli_write_prompt(&app->cli);
            }
            return;
    }

    if (sent) {
        app->announce_step++;
    }
}

static void on_broker_session(void *arg)
{
    net_announce((app_t *)arg);
}

static void on_broker_message(void *arg, const char *topic, const uint8_t *payload,
                              size_t length)
{
    app_t *app = (app_t *)arg;

    if (strcmp(topic, app->ha.topic_range_first_command) == 0 ||
        strcmp(topic, app->ha.topic_range_last_command) == 0) {
        uint32_t value;
        if (!ha_parse_range_command((const char *)payload, length, &value)) {
            cli_printf(&app->cli, "\r\n[ha] unreadable range command (%u bytes)\r\n",
                       (unsigned)length);
            cli_write_prompt(&app->cli);
            return;
        }

        if (strcmp(topic, app->ha.topic_range_first_command) == 0) {
            led_range_set_first(&app->range, value);
        } else {
            led_range_set_last(&app->range, value);
        }
        return;
    }

    if (strcmp(topic, app->ha.topic_command) != 0) {
        return;
    }

    ha_command_t command;
    if (!ha_parse_command((const char *)payload, length, &command)) {
        cli_printf(&app->cli, "\r\n[ha] unreadable command (%u bytes)\r\n",
                   (unsigned)length);
        cli_write_prompt(&app->cli);
        return;
    }

    if (command.unknown_effect) {
        /* Almost always means the published effect list and this firmware
           have drifted apart, which is worth saying out loud. */
        cli_write(&app->cli,
                  "\r\n[ha] command named an effect this firmware does not have\r\n");
        cli_write_prompt(&app->cli);
    }

    ha_apply_command(&command, &app->light, app_now_ms());
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------*/

void net_init(app_t *app)
{
    const wifi_result_t started = wifi_init(&app->wifi);

    app->radio_ready = (started == WIFI_OK);
    mqtt_init(&app->mqtt);
    app->ha_ready = ha_init(&app->ha, app->stored.device_id, app->stored.ha_prefix);

    if (started != WIFI_OK && WIFI_SUPPORTED) {
        cli_printf(&app->cli, "wifi_init: %s\r\n", wifi_result_name(started));
    }
    if (!app->ha_ready) {
        cli_write(&app->cli, "the device id is unusable; set deviceid and reboot\r\n");
    }
}

void net_start_if_configured(app_t *app)
{
    if (!app->radio_ready || !app->ha_ready) {
        return;
    }
    if (wifi_check_credentials(app->stored.ssid, app->stored.password) !=
        WIFI_CREDENTIALS_OK) {
        return;
    }

    const wifi_config_t config = wifi_config_of(app);

    if (wifi_connect(&app->wifi, &config) == WIFI_OK &&
        app->stored.broker_host[0] != '\0') {
        /*
         * Started before the link is up on purpose: mqtt_connect() is
         * documented to be callable with no network yet, and its own backoff
         * carries the attempt until there is one.
         */
        const mqtt_config_t mqtt_config = mqtt_config_of(app);
        (void)mqtt_connect(&app->mqtt, &mqtt_config);
    }
}

void net_poll(app_t *app)
{
    /* Reported only on a change, so an idle console stays quiet. Static
       because they are the console's memory of what it last said, and belong
       to nothing else. */
    static wifi_state_t reported_wifi = WIFI_STATE_IDLE;
    static mqtt_state_t reported_mqtt = MQTT_STATE_IDLE;

    wifi_poll(&app->wifi);
    mqtt_poll(&app->mqtt);
    poll_announcement(app);

    const wifi_state_t wifi_now = wifi_state(&app->wifi);
    if (wifi_now != reported_wifi) {
        reported_wifi = wifi_now;
        cli_printf(&app->cli, "\r\n[wifi] %s", wifi_state_name(wifi_now));
        if (wifi_now == WIFI_STATE_CONNECTED) {
            cli_printf(&app->cli, " as %s", wifi_address(&app->wifi));
        }
        cli_write(&app->cli, "\r\n");
        cli_write_prompt(&app->cli);
    }

    const mqtt_state_t mqtt_now = mqtt_state(&app->mqtt);
    if (mqtt_now != reported_mqtt) {
        reported_mqtt = mqtt_now;
        cli_printf(&app->cli, "\r\n[mqtt] %s\r\n", mqtt_state_name(mqtt_now));
        cli_write_prompt(&app->cli);
    }
}

/* ---------------------------------------------------------------------------
 * Console
 * -------------------------------------------------------------------------*/

static int cmd_connect(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    if (!WIFI_SUPPORTED) {
        cli_write(c, "this board has no radio\r\n");
        return CLI_ERR_STATE;
    }
    if (wifi_check_credentials(app->stored.ssid, app->stored.password) !=
        WIFI_CREDENTIALS_OK) {
        cli_write(c, "set ssid and password first\r\n");
        return CLI_ERR_STATE;
    }

    const wifi_config_t config = wifi_config_of(app);
    const wifi_result_t result = wifi_connect(&app->wifi, &config);

    if (result != WIFI_OK) {
        cli_printf(c, "error: %s\r\n", wifi_result_name(result));
        return CLI_ERR_FAILED;
    }

    if (app->stored.broker_host[0] != '\0') {
        const mqtt_config_t mqtt_config = mqtt_config_of(app);
        (void)mqtt_connect(&app->mqtt, &mqtt_config);
    }
    cli_printf(c, "associating with %s\r\n", app->stored.ssid);
    return CLI_OK;
}

static int cmd_disconnect(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    mqtt_close(&app->mqtt);
    wifi_disconnect(&app->wifi);
    cli_write(c, "disconnected\r\n");
    return CLI_OK;
}

static int cmd_announce(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    if (!mqtt_is_connected(&app->mqtt)) {
        cli_write(c, "not connected to a broker\r\n");
        return CLI_ERR_STATE;
    }

    net_announce(app);
    return CLI_OK;
}

size_t net_commands(app_t *app, cli_command_t *out, size_t capacity)
{
    static const cli_command_t table[NET_COMMAND_COUNT] = {
        { "connect",    "associate and connect to the broker", cmd_connect,    NULL },
        { "disconnect", "stop trying",                         cmd_disconnect, NULL },
        { "announce",   "republish the discovery document",    cmd_announce,   NULL },
    };

    size_t written = 0;

    for (size_t i = 0; i < NET_COMMAND_COUNT && written < capacity; i++) {
        out[written] = table[i];
        out[written].user_data = app;
        written++;
    }
    return written;
}
