/*
 * mqtt_test - bench for the mqtt component, riding on top of wifi.
 *
 * All settings -- WiFi credentials and the broker's address -- are typed in
 * over the console and kept in flash, never compiled in, for the same reason
 * wifi_test does it: DESIGN_DOC.md section 13.
 *
 * See README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "mqtt.h"
#include "persistent_config.h"
#include "wifi.h"

#define KEY_SSID "wifi_ssid"
#define KEY_PASSWORD "wifi_password"
#define KEY_HOSTNAME "wifi_hostname"
#define KEY_BROKER "mqtt_broker"
#define KEY_PORT "mqtt_port"
#define KEY_CLIENT_ID "mqtt_client_id"
#define KEY_MQTT_USER "mqtt_user"
#define KEY_MQTT_PASS "mqtt_pass"

static uint8_t config_buffer[1024];
static persistent_config_t settings;

static wifi_t wifi;
static mqtt_t mqtt;

/* Borrowed by wifi_config_t/mqtt_config_t rather than copied, so these must
   outlive the connections they configure. */
static char ssid[WIFI_SSID_MAX_LENGTH + 1];
static char password[WIFI_PASSWORD_MAX_LENGTH + 1];
static char hostname[33];
static char broker_host[128];
static char broker_port_text[8];
static char client_id[MQTT_CLIENT_ID_MAX_LENGTH + 1];
static char mqtt_user[64];
static char mqtt_pass[64];

static char line_buffer[192];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 20u];

static bool wifi_auto_connect;
static bool mqtt_auto_connect;

static void load_settings(void)
{
    config_store_t *store = persistent_config_store(&settings);
    config_get_string(store, KEY_SSID, ssid, sizeof(ssid), "");
    config_get_string(store, KEY_PASSWORD, password, sizeof(password), "");
    config_get_string(store, KEY_HOSTNAME, hostname, sizeof(hostname), "pico-framework");
    config_get_string(store, KEY_BROKER, broker_host, sizeof(broker_host), "");
    config_get_string(store, KEY_PORT, broker_port_text, sizeof(broker_port_text), "1883");
    config_get_string(store, KEY_CLIENT_ID, client_id, sizeof(client_id), "pico-mqtt-test");
    config_get_string(store, KEY_MQTT_USER, mqtt_user, sizeof(mqtt_user), "");
    config_get_string(store, KEY_MQTT_PASS, mqtt_pass, sizeof(mqtt_pass), "");
}

static wifi_config_t current_wifi_config(void)
{
    return (wifi_config_t){
        .ssid = ssid,
        .password = password,
        .hostname = hostname,
        .attempt_timeout_ms = WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS,
        .retry = {
            .first_delay_ms = 1000,
            .max_delay_ms = 15000,
            .max_attempts = 0,
        },
    };
}

/* Prints every message this session receives, since watching that arrive is
   the point of a bench. */
static void on_message(void *arg, const char *topic, const uint8_t *payload, size_t length)
{
    cli_t *c = (cli_t *)arg;
    cli_printf(c, "\r\n[mqtt] %s: %.*s\r\n", topic, (int)length, (const char *)payload);
    cli_write_prompt(c);
}

static mqtt_config_t current_mqtt_config(void)
{
    return (mqtt_config_t){
        .broker_host = broker_host,
        .broker_port = (uint16_t)atoi(broker_port_text),
        .client_id = client_id,
        .username = mqtt_user,
        .password = mqtt_pass,
        .keep_alive_s = MQTT_DEFAULT_KEEP_ALIVE_S,
        .on_message = on_message,
        .on_message_arg = &cli,
        .retry = {
            .first_delay_ms = 1000,
            .max_delay_ms = 15000,
            .max_attempts = 0,
        },
    };
}

/* ---------------------------------------------------------------------------
 * WiFi commands -- same shape as wifi_test
 * -------------------------------------------------------------------------*/

static int cmd_ssid(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "ssid %s\r\n", ssid[0] != '\0' ? ssid : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_SSID, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_password(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "password %s\r\n", password[0] != '\0' ? "<set>" : "<unset>");
        return CLI_OK;
    }
    const wifi_credentials_result_t checked = wifi_check_credentials(ssid, value);
    if (checked != WIFI_CREDENTIALS_OK && checked != WIFI_CREDENTIALS_NO_SSID) {
        cli_printf(c, "refused: %s\r\n", wifi_credentials_result_name(checked));
        return CLI_ERR_ARG;
    }
    config_set_string(persistent_config_store(&settings), KEY_PASSWORD, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_hostname(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "hostname %s\r\n", hostname);
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_HOSTNAME, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_connect(cli_t *c, void *user_data)
{
    (void)user_data;
    if (!WIFI_SUPPORTED) {
        cli_write(c, "this board has no radio\r\n");
        return CLI_ERR_STATE;
    }
    const wifi_credentials_result_t checked = wifi_check_credentials(ssid, password);
    if (checked != WIFI_CREDENTIALS_OK) {
        cli_printf(c, "credentials: %s\r\n", wifi_credentials_result_name(checked));
        return CLI_ERR_STATE;
    }
    const wifi_config_t config = current_wifi_config();
    const wifi_result_t result = wifi_connect(&wifi, &config);
    if (result != WIFI_OK) {
        cli_printf(c, "error: %s\r\n", wifi_result_name(result));
        return CLI_ERR_FAILED;
    }
    wifi_auto_connect = true;
    cli_printf(c, "associating with %s; watch wifistatus\r\n", ssid);
    return CLI_OK;
}

static int cmd_disconnect(cli_t *c, void *user_data)
{
    (void)user_data;
    wifi_auto_connect = false;
    wifi_disconnect(&wifi);
    cli_write(c, "disconnected\r\n");
    return CLI_OK;
}

static int cmd_wifistatus(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "radio     %s\r\n", WIFI_SUPPORTED ? "present" : "none on this board");
    cli_printf(c, "state     %s\r\n", wifi_state_name(wifi_state(&wifi)));
    cli_printf(c, "ssid      %s\r\n", ssid[0] != '\0' ? ssid : "<unset>");
    cli_printf(c, "address   %s\r\n", wifi_address(&wifi));
    if (wifi_is_connected(&wifi)) {
        cli_printf(c, "rssi      %ld dBm\r\n", (long)wifi_rssi(&wifi));
    } else {
        cli_printf(c, "attempts  %lu\r\n", (unsigned long)wifi_attempts(&wifi));
    }
    return CLI_OK;
}

/* ---------------------------------------------------------------------------
 * Broker settings
 * -------------------------------------------------------------------------*/

static int cmd_broker(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "broker %s\r\n", broker_host[0] != '\0' ? broker_host : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_BROKER, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_port(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "port %s\r\n", broker_port_text);
        return CLI_OK;
    }
    if (value == 0 || value > 65535u) {
        cli_write(c, "refused: out of range\r\n");
        return CLI_ERR_RANGE;
    }
    char text[8];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    config_set_string(persistent_config_store(&settings), KEY_PORT, text);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_clientid(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "clientid %s\r\n", client_id);
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_CLIENT_ID, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_mqttuser(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "mqttuser %s\r\n", mqtt_user[0] != '\0' ? mqtt_user : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_MQTT_USER, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_mqttpass(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "mqttpass %s\r\n", mqtt_pass[0] != '\0' ? "<set>" : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_MQTT_PASS, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_save(cli_t *c, void *user_data)
{
    (void)user_data;
    const persistent_config_result_t result = persistent_config_save(&settings);
    cli_printf(c, "%s\r\n", persistent_config_result_name(result));
    return result == PERSISTENT_CONFIG_OK ? CLI_OK : CLI_ERR_FAILED;
}

/* ---------------------------------------------------------------------------
 * MQTT commands
 * -------------------------------------------------------------------------*/

static int cmd_mqttconnect(cli_t *c, void *user_data)
{
    (void)user_data;
    if (!MQTT_SUPPORTED) {
        cli_write(c, "this board has no network stack\r\n");
        return CLI_ERR_STATE;
    }
    if (broker_host[0] == '\0') {
        cli_write(c, "set a broker first\r\n");
        return CLI_ERR_STATE;
    }
    const mqtt_config_t config = current_mqtt_config();
    const mqtt_result_t result = mqtt_connect(&mqtt, &config);
    if (result != MQTT_OK) {
        cli_printf(c, "error: %s\r\n", mqtt_result_name(result));
        return CLI_ERR_FAILED;
    }
    mqtt_auto_connect = true;
    cli_printf(c, "connecting to %s:%s; watch mqttstatus\r\n", broker_host, broker_port_text);
    return CLI_OK;
}

static int cmd_mqttdisconnect(cli_t *c, void *user_data)
{
    (void)user_data;
    mqtt_auto_connect = false;
    mqtt_close(&mqtt);
    cli_write(c, "disconnected\r\n");
    return CLI_OK;
}

static int cmd_mqttstatus(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "network      %s\r\n", MQTT_SUPPORTED ? "present" : "none on this board");
    cli_printf(c, "state        %s\r\n", mqtt_state_name(mqtt_state(&mqtt)));
    cli_printf(c, "broker       %s:%s\r\n", broker_host[0] != '\0' ? broker_host : "<unset>",
               broker_port_text);
    cli_printf(c, "client id    %s\r\n", client_id);
    if (!mqtt_is_connected(&mqtt)) {
        cli_printf(c, "attempts     %lu\r\n", (unsigned long)mqtt_attempts(&mqtt));
    }
    cli_printf(c, "dropped      %lu\r\n", (unsigned long)mqtt_messages_dropped(&mqtt));
    return CLI_OK;
}

static int cmd_sub(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *topic = cli_next_token(c);
    if (topic == NULL) {
        cli_write(c, "usage: sub <topic> [qos]\r\n");
        return CLI_ERR_ARG;
    }
    uint32_t qos = 0;
    cli_next_u32(c, &qos); /* defaults to 0 when absent or malformed */

    const mqtt_result_t result = mqtt_subscribe_topic(&mqtt, topic, (uint8_t)qos);
    cli_printf(c, "%s\r\n", mqtt_result_name(result));
    return (result == MQTT_OK) ? CLI_OK : CLI_ERR_FAILED;
}

static int cmd_unsub(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *topic = cli_next_token(c);
    if (topic == NULL) {
        cli_write(c, "usage: unsub <topic>\r\n");
        return CLI_ERR_ARG;
    }
    const mqtt_result_t result = mqtt_unsubscribe_topic(&mqtt, topic);
    cli_printf(c, "%s\r\n", mqtt_result_name(result));
    return (result == MQTT_OK) ? CLI_OK : CLI_ERR_FAILED;
}

static int cmd_pub(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *topic = cli_next_token(c);
    const char *message = cli_rest(c);
    if (topic == NULL || message == NULL) {
        cli_write(c, "usage: pub <topic> <message>\r\n");
        return CLI_ERR_ARG;
    }
    const mqtt_result_t result =
        mqtt_publish_message(&mqtt, topic, message, (uint16_t)strlen(message), 0, false);
    cli_printf(c, "%s\r\n", mqtt_result_name(result));
    return (result == MQTT_OK) ? CLI_OK : CLI_ERR_FAILED;
}

static const cli_command_t own_commands[] = {
    { "ssid",            "ssid [name] - show or set",              cmd_ssid,            NULL },
    { "password",        "password [value] - set only",            cmd_password,        NULL },
    { "hostname",        "hostname [name] - show or set",          cmd_hostname,        NULL },
    { "connect",         "associate, and keep reconnecting",       cmd_connect,         NULL },
    { "disconnect",      "stop trying",                            cmd_disconnect,      NULL },
    { "wifistatus",      "wifi state, address and signal",         cmd_wifistatus,      NULL },

    { "broker",          "broker [host] - show or set",            cmd_broker,          NULL },
    { "port",            "port [n] - show or set",                 cmd_port,            NULL },
    { "clientid",        "clientid [id] - show or set",            cmd_clientid,        NULL },
    { "mqttuser",        "mqttuser [name] - show or set",          cmd_mqttuser,        NULL },
    { "mqttpass",        "mqttpass [value] - set only",            cmd_mqttpass,        NULL },
    { "save",            "keep every setting in flash",            cmd_save,            NULL },

    { "mqttconnect",     "connect to the broker, and keep retrying", cmd_mqttconnect,   NULL },
    { "mqttdisconnect",  "stop trying",                            cmd_mqttdisconnect,  NULL },
    { "mqttstatus",      "mqtt state, broker, attempts, dropped",  cmd_mqttstatus,      NULL },
    { "sub",             "sub <topic> [qos] - subscribe",          cmd_sub,             NULL },
    { "unsub",           "unsub <topic> - unsubscribe",            cmd_unsub,           NULL },
    { "pub",             "pub <topic> <message> - publish, qos 0", cmd_pub,             NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    persistent_config_load(&settings, config_buffer, sizeof(config_buffer));
    load_settings();

    const wifi_result_t wifi_started = wifi_init(&wifi);
    mqtt_init(&mqtt);

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
        .prompt = "mqtt> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nmqtt_test  board %s  radio %s\r\n", PICO_BOARD,
               WIFI_SUPPORTED ? "present" : "none");
    if (wifi_started != WIFI_OK) {
        cli_printf(&cli, "wifi_init: %s\r\n", wifi_result_name(wifi_started));
    }
    cli_write(&cli, "type help. Set ssid/password/broker, save, then connect and mqttconnect\r\n");
    cli_write_prompt(&cli);

    /* Connect on their own if settings were already stored, so a deployed
       instance needs no console after the first setup. */
    if (wifi_started == WIFI_OK &&
        wifi_check_credentials(ssid, password) == WIFI_CREDENTIALS_OK) {
        const wifi_config_t config = current_wifi_config();
        if (wifi_connect(&wifi, &config) == WIFI_OK) {
            wifi_auto_connect = true;
            cli_printf(&cli, "stored wifi credentials found; associating with %s\r\n", ssid);
        }
    }
    if (broker_host[0] != '\0') {
        const mqtt_config_t config = current_mqtt_config();
        if (mqtt_connect(&mqtt, &config) == MQTT_OK) {
            mqtt_auto_connect = true;
            cli_printf(&cli, "stored broker found; connecting to %s\r\n", broker_host);
        }
    }

    wifi_state_t reported_wifi = WIFI_STATE_IDLE;
    mqtt_state_t reported_mqtt = MQTT_STATE_IDLE;

    while (true) {
        cli_poll(&cli);
        wifi_poll(&wifi);
        mqtt_poll(&mqtt);

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

        sleep_ms(1);
    }
}
