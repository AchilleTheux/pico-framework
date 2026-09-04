/*
 * tcp_test - bench for the tcp component, riding on top of wifi.
 *
 * WiFi credentials and the peer's address are typed in over the console and
 * kept in flash, never compiled in, for the same reason wifi_test and
 * mqtt_test do it: DESIGN_DOC.md section 13.
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
#include "persistent_config.h"
#include "tcp.h"
#include "wifi.h"

#define KEY_SSID "wifi_ssid"
#define KEY_PASSWORD "wifi_password"
#define KEY_HOSTNAME "wifi_hostname"
#define KEY_HOST "tcp_host"
#define KEY_PORT "tcp_port"

static uint8_t config_buffer[1024];
static persistent_config_t settings;

static wifi_t wifi;
static tcp_client_t client;

/* Borrowed by wifi_config_t/tcp_client_config_t rather than copied, so these
   must outlive the connections they configure. */
static char ssid[WIFI_SSID_MAX_LENGTH + 1];
static char password[WIFI_PASSWORD_MAX_LENGTH + 1];
static char hostname[33];
static char peer_host[128];
static char peer_port_text[8];

/* Caller-owned, as the component requires, and deliberately small: a bench
   that never fills its buffers never exercises what happens when they fill. */
static uint8_t rx_buffer[512];
static uint8_t tx_buffer[512];

static char line_buffer[192];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 16u];

static bool auto_reconnect = true;

static void load_settings(void)
{
    config_store_t *store = persistent_config_store(&settings);
    config_get_string(store, KEY_SSID, ssid, sizeof(ssid), "");
    config_get_string(store, KEY_PASSWORD, password, sizeof(password), "");
    config_get_string(store, KEY_HOSTNAME, hostname, sizeof(hostname), "pico-framework");
    config_get_string(store, KEY_HOST, peer_host, sizeof(peer_host), "");
    config_get_string(store, KEY_PORT, peer_port_text, sizeof(peer_port_text), "5000");
}

static uint16_t peer_port(void)
{
    return (uint16_t)strtoul(peer_port_text, NULL, 10);
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

static void on_tcp_connected(void *arg)
{
    cli_t *c = (cli_t *)arg;
    cli_write(c, "\r\n[tcp] connected\r\n");
    cli_write_prompt(c);
}

static void on_tcp_closed(void *arg)
{
    cli_t *c = (cli_t *)arg;
    cli_write(c, "\r\n[tcp] closed by the far end\r\n");
    cli_write_prompt(c);
}

static tcp_client_config_t current_tcp_config(void)
{
    return (tcp_client_config_t){
        .host = peer_host,
        .port = peer_port(),
        .rx_buffer = rx_buffer,
        .rx_buffer_size = sizeof(rx_buffer),
        .tx_buffer = tx_buffer,
        .tx_buffer_size = sizeof(tx_buffer),
        .connect_timeout_ms = TCP_DEFAULT_CONNECT_TIMEOUT_MS,
        .on_connect = on_tcp_connected,
        .on_connect_arg = &cli,
        .on_closed = on_tcp_closed,
        .on_closed_arg = &cli,
        .auto_reconnect = auto_reconnect,
        .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
    };
}

/* ---------------------------------------------------------------------------
 * WiFi settings
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
    config_set_string(persistent_config_store(&settings), KEY_PASSWORD, value);
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
    cli_printf(c, "associating with %s; watch wifistatus\r\n", ssid);
    return CLI_OK;
}

static int cmd_wifistatus(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "radio     %s\r\n", WIFI_SUPPORTED ? "present" : "none on this board");
    cli_printf(c, "state     %s\r\n", wifi_state_name(wifi_state(&wifi)));
    cli_printf(c, "address   %s\r\n", wifi_address(&wifi));
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
 * Peer settings
 * -------------------------------------------------------------------------*/

static int cmd_host(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "host %s\r\n", peer_host[0] != '\0' ? peer_host : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_HOST, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_port(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "port %s\r\n", peer_port_text);
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

/* ---------------------------------------------------------------------------
 * TCP commands
 * -------------------------------------------------------------------------*/

static int cmd_open(cli_t *c, void *user_data)
{
    (void)user_data;
    if (peer_host[0] == '\0') {
        cli_write(c, "set host first\r\n");
        return CLI_ERR_STATE;
    }
    const tcp_client_config_t config = current_tcp_config();
    const tcp_result_t result = tcp_client_open(&client, &config);
    if (result != TCP_OK) {
        cli_printf(c, "error: %s\r\n", tcp_client_result_name(result));
        return CLI_ERR_FAILED;
    }
    cli_printf(c, "connecting to %s:%u\r\n", peer_host, (unsigned)peer_port());
    return CLI_OK;
}

static int cmd_close(cli_t *c, void *user_data)
{
    (void)user_data;
    tcp_client_close(&client);
    cli_write(c, "closed\r\n");
    return CLI_OK;
}

static int cmd_send(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *text = cli_rest(c);
    if (text == NULL) {
        cli_write(c, "usage: send <text>\r\n");
        return CLI_ERR_ARG;
    }
    if (!tcp_client_is_connected(&client)) {
        cli_write(c, "not connected\r\n");
        return CLI_ERR_STATE;
    }

    const size_t length = strlen(text);
    const size_t accepted = tcp_client_write(&client, text, length);
    tcp_client_write(&client, "\r\n", 2);

    if (accepted < length) {
        cli_printf(c, "buffer full: %u of %u bytes queued\r\n",
                   (unsigned)accepted, (unsigned)length);
        return CLI_ERR_FAILED;
    }
    cli_printf(c, "queued %u bytes\r\n", (unsigned)(length + 2u));
    return CLI_OK;
}

/*
 * Write far more than the outgoing buffer holds, in one go.
 *
 * This is the case a bench exists for: the component has to keep the stream in
 * order across however many partial sends the network imposes, and the far end
 * has to receive the pattern unbroken. A mismatch on the far end is a
 * reordering bug; a short count here is the buffer being too small, which is
 * reported rather than hidden.
 */
static int cmd_flood(cli_t *c, void *user_data)
{
    (void)user_data;
    uint32_t count;
    if (!cli_next_u32(c, &count)) {
        cli_write(c, "usage: flood <lines>\r\n");
        return CLI_ERR_ARG;
    }
    if (!tcp_client_is_connected(&client)) {
        cli_write(c, "not connected\r\n");
        return CLI_ERR_STATE;
    }

    uint32_t queued = 0;
    for (uint32_t i = 0; i < count; i++) {
        char line[32];
        const int n = snprintf(line, sizeof(line), "line %lu\r\n", (unsigned long)i);
        if (n <= 0) {
            break;
        }

        /*
         * The buffer fills long before `count` lines fit, so a refusal is the
         * normal case: poll to drain it into lwIP and offer the same line
         * again, which is exactly what an application with more to say than
         * fits has to do.
         *
         * Bounded, because the connection can drop in the middle of this and a
         * disconnected stream refuses every write. Retrying that forever would
         * hang the console with no way out.
         */
        unsigned attempts = 0;
        while (tcp_client_write(&client, line, (size_t)n) != (size_t)n) {
            if (!tcp_client_is_connected(&client)) {
                cli_printf(c, "connection lost after %lu lines\r\n",
                           (unsigned long)queued);
                return CLI_ERR_STATE;
            }
            if (++attempts > 1000u) {
                cli_printf(c, "stalled after %lu lines; peer is not reading\r\n",
                           (unsigned long)queued);
                return CLI_ERR_FAILED;
            }
            tcp_client_poll(&client);
            wifi_poll(&wifi);
        }
        queued++;
    }
    cli_printf(c, "queued %lu lines\r\n", (unsigned long)queued);
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "state      %s\r\n", tcp_client_state_name(tcp_client_state(&client)));
    cli_printf(c, "peer       %s:%u\r\n", peer_host[0] != '\0' ? peer_host : "<unset>",
               (unsigned)peer_port());
    cli_printf(c, "sessions   %lu\r\n", (unsigned long)tcp_client_sessions(&client));
    cli_printf(c, "attempts   %lu\r\n", (unsigned long)tcp_client_attempts(&client));
    cli_printf(c, "sent       %lu bytes\r\n", (unsigned long)tcp_client_bytes_sent(&client));
    cli_printf(c, "received   %lu bytes\r\n", (unsigned long)tcp_client_bytes_received(&client));
    cli_printf(c, "pending    %u bytes\r\n", (unsigned)tcp_client_pending(&client));
    cli_printf(c, "unread     %u bytes\r\n", (unsigned)tcp_client_available(&client));
    cli_printf(c, "dropped    %lu bytes\r\n", (unsigned long)tcp_client_dropped(&client));
    return CLI_OK;
}

static int cmd_reconnect(cli_t *c, void *user_data)
{
    (void)user_data;
    auto_reconnect = !auto_reconnect;
    cli_printf(c, "auto-reconnect %s (re-open to apply)\r\n", auto_reconnect ? "on" : "off");
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "ssid",       "ssid [name] - show or set the WiFi SSID",        cmd_ssid,       NULL },
    { "password",   "password [text] - set the WiFi passphrase",      cmd_password,   NULL },
    { "connect",    "connect - associate with the stored network",    cmd_connect,    NULL },
    { "wifistatus", "wifistatus - radio and link state",              cmd_wifistatus, NULL },
    { "save",       "save - write settings to flash",                 cmd_save,       NULL },
    { "host",       "host [name|ip] - show or set the peer",          cmd_host,       NULL },
    { "port",       "port [n] - show or set the peer port",           cmd_port,       NULL },
    { "open",       "open - connect to the peer",                     cmd_open,       NULL },
    { "close",      "close - close the connection",                   cmd_close,      NULL },
    { "send",       "send <text> - send one line",                    cmd_send,       NULL },
    { "flood",      "flood <lines> - send more than the buffer holds", cmd_flood,     NULL },
    { "status",     "status - connection state and counters",         cmd_status,     NULL },
    { "reconnect",  "reconnect - toggle automatic reconnection",      cmd_reconnect,  NULL },
};

/* Anything the peer sends is printed as it arrives: watching that is the point
   of a bench, and it makes the connection an echo test with any `nc` on the
   other end. */
static void drain_incoming(void)
{
    if (tcp_client_available(&client) == 0) {
        return;
    }

    char chunk[65];
    const size_t got = tcp_client_read_bytes(&client, chunk, sizeof(chunk) - 1u);
    if (got == 0) {
        return;
    }
    chunk[got] = '\0';
    cli_printf(&cli, "\r\n[rx] %s\r\n", chunk);
    cli_write_prompt(&cli);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    persistent_config_load(&settings, config_buffer, sizeof(config_buffer));
    load_settings();

    const wifi_result_t wifi_started = wifi_init(&wifi);
    tcp_client_init(&client);

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
        .prompt = "tcp> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\ntcp_test  board %s  radio %s\r\n", PICO_BOARD,
               WIFI_SUPPORTED ? "present" : "none");
    if (wifi_started != WIFI_OK) {
        cli_printf(&cli, "wifi_init: %s\r\n", wifi_result_name(wifi_started));
    }
    cli_write(&cli, "type help. Set ssid/password/host/port, save, connect, then open\r\n");
    cli_write_prompt(&cli);

    /* Associate on their own if settings were already stored, so a second run
       needs no console. */
    if (wifi_started == WIFI_OK &&
        wifi_check_credentials(ssid, password) == WIFI_CREDENTIALS_OK) {
        const wifi_config_t config = current_wifi_config();
        if (wifi_connect(&wifi, &config) == WIFI_OK) {
            cli_printf(&cli, "stored credentials found; associating with %s\r\n", ssid);
        }
    }

    wifi_state_t reported_wifi = WIFI_STATE_IDLE;
    tcp_state_t reported_tcp = TCP_STATE_IDLE;

    while (true) {
        cli_poll(&cli);
        wifi_poll(&wifi);
        tcp_client_poll(&client);
        drain_incoming();

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

        const tcp_state_t tcp_now = tcp_client_state(&client);
        if (tcp_now != reported_tcp) {
            reported_tcp = tcp_now;
            cli_printf(&cli, "\r\n[tcp] %s\r\n", tcp_client_state_name(tcp_now));
            cli_write_prompt(&cli);
        }

        sleep_ms(1);
    }
}
