/*
 * udp_test - bench for the udp component, riding on top of wifi.
 *
 * WiFi credentials and the peer's address are typed in over the console and
 * kept in flash, never compiled in: DESIGN_DOC.md section 13.
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
#include "udp.h"
#include "wifi.h"

#define KEY_SSID "wifi_ssid"
#define KEY_PASSWORD "wifi_password"
#define KEY_HOSTNAME "wifi_hostname"
#define KEY_PEER "udp_peer"
#define KEY_PEER_PORT "udp_peer_port"
#define KEY_LOCAL_PORT "udp_local_port"

static uint8_t config_buffer[1024];
static persistent_config_t settings;

static wifi_t wifi;
static udp_socket_t socket_instance;

/* Borrowed by wifi_config_t rather than copied, so these must outlive the
   connection they configure. */
static char ssid[WIFI_SSID_MAX_LENGTH + 1];
static char password[WIFI_PASSWORD_MAX_LENGTH + 1];
static char hostname[33];
static char peer_host[128];
static char peer_port_text[8];
static char local_port_text[8];

static char line_buffer[192];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 16u];

/* Echo every datagram back where it came from. Two boards then ping-pong, and
   a laptop running `nc -u` gets its own text back, which makes a round trip
   visible without writing anything on the other end. */
static bool echo_replies;

static void load_settings(void)
{
    config_store_t *store = persistent_config_store(&settings);
    config_get_string(store, KEY_SSID, ssid, sizeof(ssid), "");
    config_get_string(store, KEY_PASSWORD, password, sizeof(password), "");
    config_get_string(store, KEY_HOSTNAME, hostname, sizeof(hostname), "pico-framework");
    config_get_string(store, KEY_PEER, peer_host, sizeof(peer_host), "");
    config_get_string(store, KEY_PEER_PORT, peer_port_text, sizeof(peer_port_text), "5005");
    config_get_string(store, KEY_LOCAL_PORT, local_port_text, sizeof(local_port_text), "5005");
}

static uint16_t peer_port(void)
{
    return (uint16_t)strtoul(peer_port_text, NULL, 10);
}

static uint16_t configured_local_port(void)
{
    return (uint16_t)strtoul(local_port_text, NULL, 10);
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

/*
 * Runs from lwIP's receive path inside wifi_poll(). Printing and one reply are
 * both small enough to belong here; anything longer would go on a flag for the
 * main loop, as udp.h says.
 */
static void on_datagram(void *arg, const udp_endpoint_t *from,
                        const uint8_t *data, size_t length)
{
    cli_t *c = (cli_t *)arg;

    char address[UDP_ADDRESS_LENGTH];
    udp_ipv4_format(from->address, address, sizeof(address));

    cli_printf(c, "\r\n[rx] %s:%u %u bytes\r\n", address, (unsigned)from->port,
               (unsigned)length);
    cli_printf(c, "     %.*s\r\n", (int)(length > 64u ? 64u : length), (const char *)data);

    if (echo_replies) {
        udp_socket_send_to_endpoint(&socket_instance, from, data, length);
    }
    cli_write_prompt(c);
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
 * Peer and socket settings
 * -------------------------------------------------------------------------*/

static int cmd_peer(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "peer %s\r\n", peer_host[0] != '\0' ? peer_host : "<unset>");
        return CLI_OK;
    }
    config_set_string(persistent_config_store(&settings), KEY_PEER, value);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int set_port_setting(cli_t *c, const char *key, const char *current)
{
    uint32_t value;
    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "%s\r\n", current);
        return CLI_OK;
    }
    if (value > 65535u) {
        cli_write(c, "refused: out of range\r\n");
        return CLI_ERR_RANGE;
    }
    char text[8];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    config_set_string(persistent_config_store(&settings), key, text);
    load_settings();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_peerport(cli_t *c, void *user_data)
{
    (void)user_data;
    return set_port_setting(c, KEY_PEER_PORT, peer_port_text);
}

static int cmd_localport(cli_t *c, void *user_data)
{
    (void)user_data;
    return set_port_setting(c, KEY_LOCAL_PORT, local_port_text);
}

/* ---------------------------------------------------------------------------
 * UDP commands
 * -------------------------------------------------------------------------*/

static int cmd_bind(cli_t *c, void *user_data)
{
    (void)user_data;
    const udp_socket_config_t config = {
        .local_port = configured_local_port(),
        .broadcast = true,
        .on_datagram = on_datagram,
        .on_datagram_arg = &cli,
    };

    const udp_result_t result = udp_socket_open(&socket_instance, &config);
    if (result != UDP_OK) {
        cli_printf(c, "error: %s\r\n", udp_result_name(result));
        return CLI_ERR_FAILED;
    }
    cli_printf(c, "listening on port %u\r\n",
               (unsigned)udp_socket_local_port(&socket_instance));
    return CLI_OK;
}

static int cmd_close(cli_t *c, void *user_data)
{
    (void)user_data;
    udp_socket_close(&socket_instance);
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
    if (peer_host[0] == '\0') {
        cli_write(c, "set peer first\r\n");
        return CLI_ERR_STATE;
    }

    const udp_result_t result = udp_socket_send_to(&socket_instance, peer_host,
                                                   peer_port(), text, strlen(text));
    if (result == UDP_ERR_RESOLVING) {
        /* Documented and deliberate: the lookup has started, nothing was sent.
           Repeating the command is the contract. */
        cli_write(c, "resolving; run send again in a moment\r\n");
        return CLI_ERR_STATE;
    }
    if (result != UDP_OK) {
        cli_printf(c, "error: %s\r\n", udp_result_name(result));
        return CLI_ERR_FAILED;
    }
    cli_printf(c, "sent %u bytes to %s:%u\r\n", (unsigned)strlen(text), peer_host,
               (unsigned)peer_port());
    return CLI_OK;
}

static int cmd_bcast(cli_t *c, void *user_data)
{
    (void)user_data;
    const char *text = cli_rest(c);
    if (text == NULL) {
        cli_write(c, "usage: bcast <text>\r\n");
        return CLI_ERR_ARG;
    }

    const udp_result_t result = udp_socket_broadcast(&socket_instance, peer_port(),
                                                      text, strlen(text));
    if (result != UDP_OK) {
        cli_printf(c, "error: %s\r\n", udp_result_name(result));
        return CLI_ERR_FAILED;
    }
    cli_printf(c, "broadcast %u bytes to port %u\r\n", (unsigned)strlen(text),
               (unsigned)peer_port());
    return CLI_OK;
}

/* Send one datagram longer than UDP_MAX_PAYLOAD, which must be refused at the
   call rather than fragmented or silently lost. */
static int cmd_toolong(cli_t *c, void *user_data)
{
    (void)user_data;
    static uint8_t oversized[UDP_MAX_PAYLOAD + 1];
    memset(oversized, 'a', sizeof(oversized));

    if (peer_host[0] == '\0') {
        cli_write(c, "set peer first\r\n");
        return CLI_ERR_STATE;
    }

    const udp_result_t result = udp_socket_send_to(&socket_instance, peer_host,
                                                   peer_port(), oversized,
                                                   sizeof(oversized));
    cli_printf(c, "%u bytes: %s\r\n", (unsigned)sizeof(oversized),
               udp_result_name(result));
    return result == UDP_ERR_TOO_LONG ? CLI_OK : CLI_ERR_FAILED;
}

static int cmd_echo(cli_t *c, void *user_data)
{
    (void)user_data;
    echo_replies = !echo_replies;
    cli_printf(c, "echo %s\r\n", echo_replies ? "on" : "off");
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "open       %s\r\n", udp_socket_is_open(&socket_instance) ? "yes" : "no");
    cli_printf(c, "local port %u\r\n", (unsigned)udp_socket_local_port(&socket_instance));
    cli_printf(c, "peer       %s:%u\r\n", peer_host[0] != '\0' ? peer_host : "<unset>",
               (unsigned)peer_port());
    cli_printf(c, "echo       %s\r\n", echo_replies ? "on" : "off");
    cli_printf(c, "sent       %lu datagrams\r\n",
               (unsigned long)udp_socket_datagrams_sent(&socket_instance));
    cli_printf(c, "received   %lu datagrams\r\n",
               (unsigned long)udp_socket_datagrams_received(&socket_instance));
    cli_printf(c, "dropped    %lu (too long for the buffer)\r\n",
               (unsigned long)udp_socket_datagrams_dropped(&socket_instance));
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "ssid",       "ssid [name] - show or set the WiFi SSID",         cmd_ssid,      NULL },
    { "password",   "password [text] - set the WiFi passphrase",       cmd_password,  NULL },
    { "connect",    "connect - associate with the stored network",     cmd_connect,   NULL },
    { "wifistatus", "wifistatus - radio and link state",               cmd_wifistatus, NULL },
    { "save",       "save - write settings to flash",                  cmd_save,      NULL },
    { "peer",       "peer [name|ip] - show or set the peer",           cmd_peer,      NULL },
    { "peerport",   "peerport [n] - show or set the peer port",        cmd_peerport,  NULL },
    { "localport",  "localport [n] - port to bind (0 = any)",          cmd_localport, NULL },
    { "bind",       "bind - open the socket and start receiving",      cmd_bind,      NULL },
    { "close",      "close - close the socket",                        cmd_close,     NULL },
    { "send",       "send <text> - one datagram to the peer",          cmd_send,      NULL },
    { "bcast",      "bcast <text> - one datagram to every host",       cmd_bcast,     NULL },
    { "toolong",    "toolong - send past UDP_MAX_PAYLOAD, expect a refusal", cmd_toolong, NULL },
    { "echo",       "echo - toggle replying to every datagram",        cmd_echo,      NULL },
    { "status",     "status - socket state and counters",              cmd_status,    NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    persistent_config_load(&settings, config_buffer, sizeof(config_buffer));
    load_settings();

    const wifi_result_t wifi_started = wifi_init(&wifi);
    udp_socket_init(&socket_instance);

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
        .prompt = "udp> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nudp_test  board %s  radio %s\r\n", PICO_BOARD,
               WIFI_SUPPORTED ? "present" : "none");
    if (wifi_started != WIFI_OK) {
        cli_printf(&cli, "wifi_init: %s\r\n", wifi_result_name(wifi_started));
    }
    cli_write(&cli, "type help. Set ssid/password/peer, save, connect, then bind\r\n");
    cli_write_prompt(&cli);

    if (wifi_started == WIFI_OK &&
        wifi_check_credentials(ssid, password) == WIFI_CREDENTIALS_OK) {
        const wifi_config_t config = current_wifi_config();
        if (wifi_connect(&wifi, &config) == WIFI_OK) {
            cli_printf(&cli, "stored credentials found; associating with %s\r\n", ssid);
        }
    }

    wifi_state_t reported_wifi = WIFI_STATE_IDLE;

    while (true) {
        cli_poll(&cli);
        wifi_poll(&wifi);

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

        sleep_ms(1);
    }
}
