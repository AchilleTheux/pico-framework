/*
 * wifi_test - bench for the wifi component.
 *
 * Credentials are typed in over the console and kept in flash, never compiled
 * in. That is the point as much as the connecting is: the firmware this
 * framework draws on had its SSID and passphrase as string literals in the
 * source, which is exactly what DESIGN_DOC.md section 13 forbids.
 *
 * See README.md.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "persistent_config.h"
#include "wifi.h"

/* Keys the credentials live under. */
#define KEY_SSID "wifi_ssid"
#define KEY_PASSWORD "wifi_password"
#define KEY_HOSTNAME "wifi_hostname"

static uint8_t config_buffer[1024];
static persistent_config_t settings;

static wifi_t wifi;

/* Borrowed by wifi_config_t rather than copied, so these must outlive it. */
static char ssid[WIFI_SSID_MAX_LENGTH + 1];
static char password[WIFI_PASSWORD_MAX_LENGTH + 1];
static char hostname[33];

static char line_buffer[160];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 10u];

static bool auto_connect;

static void load_credentials(void)
{
    config_store_t *store = persistent_config_store(&settings);
    config_get_string(store, KEY_SSID, ssid, sizeof(ssid), "");
    config_get_string(store, KEY_PASSWORD, password, sizeof(password), "");
    config_get_string(store, KEY_HOSTNAME, hostname, sizeof(hostname), "pico-framework");
}

static wifi_config_t current_config(void)
{
    return (wifi_config_t){
        .ssid = ssid,
        .password = password,
        .hostname = hostname,
        .attempt_timeout_ms = WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS,
        .retry = {
            .first_delay_ms = 1000,
            .max_delay_ms = 15000,
            .max_attempts = 0,    /* keep trying, as a robot should */
        },
    };
}

static int cmd_ssid(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *value = cli_rest(c);
    if (value == NULL) {
        cli_printf(c, "ssid %s\r\n", ssid[0] != '\0' ? ssid : "<unset>");
        return CLI_OK;
    }

    config_result_t result =
        config_set_string(persistent_config_store(&settings), KEY_SSID, value);
    if (result != CONFIG_OK) {
        cli_printf(c, "error: %s\r\n", config_result_name(result));
        return CLI_ERR_FAILED;
    }
    load_credentials();
    cli_write(c, "set (run save to keep it)\r\n");
    return CLI_OK;
}

static int cmd_password(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *value = cli_rest(c);
    if (value == NULL) {
        /*
         * Never echoed back. A console log is the last place a passphrase
         * should end up, and "is it set" is the only question worth answering.
         */
        cli_printf(c, "password %s\r\n",
                   password[0] != '\0' ? "<set>" : "<unset>");
        return CLI_OK;
    }

    const wifi_credentials_result_t checked = wifi_check_credentials(ssid, value);
    if (checked != WIFI_CREDENTIALS_OK && checked != WIFI_CREDENTIALS_NO_SSID) {
        cli_printf(c, "refused: %s\r\n", wifi_credentials_result_name(checked));
        return CLI_ERR_ARG;
    }

    config_result_t result =
        config_set_string(persistent_config_store(&settings), KEY_PASSWORD, value);
    if (result != CONFIG_OK) {
        cli_printf(c, "error: %s\r\n", config_result_name(result));
        return CLI_ERR_FAILED;
    }
    load_credentials();
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
    load_credentials();
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

    const wifi_config_t config = current_config();
    const wifi_result_t result = wifi_connect(&wifi, &config);
    if (result != WIFI_OK) {
        cli_printf(c, "error: %s\r\n", wifi_result_name(result));
        return CLI_ERR_FAILED;
    }

    auto_connect = true;
    cli_printf(c, "associating with %s; watch wifistatus\r\n", ssid);
    return CLI_OK;
}

static int cmd_disconnect(cli_t *c, void *user_data)
{
    (void)user_data;
    auto_connect = false;
    wifi_disconnect(&wifi);
    cli_write(c, "disconnected\r\n");
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;

    cli_printf(c, "radio     %s\r\n", WIFI_SUPPORTED ? "present" : "none on this board");
    cli_printf(c, "state     %s\r\n", wifi_state_name(wifi_state(&wifi)));
    cli_printf(c, "ssid      %s\r\n", ssid[0] != '\0' ? ssid : "<unset>");
    cli_printf(c, "password  %s\r\n", password[0] != '\0' ? "<set>" : "<unset>");
    cli_printf(c, "hostname  %s\r\n", hostname);
    cli_printf(c, "address   %s\r\n", wifi_address(&wifi));

    if (wifi_is_connected(&wifi)) {
        cli_printf(c, "rssi      %ld dBm\r\n", (long)wifi_rssi(&wifi));
    } else {
        cli_printf(c, "attempts  %lu\r\n", (unsigned long)wifi_attempts(&wifi));
    }
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "ssid",       "ssid [name] - show or set",          cmd_ssid,       NULL },
    { "password",   "password [value] - set only",        cmd_password,   NULL },
    { "hostname",   "hostname [name] - show or set",      cmd_hostname,   NULL },
    { "save",       "keep the credentials in flash",      cmd_save,       NULL },
    { "connect",    "associate, and keep reconnecting",   cmd_connect,    NULL },
    { "disconnect", "stop trying",                       cmd_disconnect, NULL },
    { "wifistatus", "state, address and signal",          cmd_status,     NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    persistent_config_load(&settings, config_buffer, sizeof(config_buffer));
    load_credentials();

    const wifi_result_t started = wifi_init(&wifi);

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
        .prompt = "wifi> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nwifi_test  board %s  radio %s\r\n", PICO_BOARD,
               WIFI_SUPPORTED ? "present" : "none");
    if (started != WIFI_OK) {
        cli_printf(&cli, "wifi_init: %s\r\n", wifi_result_name(started));
    }
    cli_write(&cli, "type help. Set ssid and password, save, then connect\r\n");
    cli_write_prompt(&cli);

    /* Connect on its own if credentials were already stored, which is what a
       deployed robot wants — no console needed after the first setup. */
    if (started == WIFI_OK &&
        wifi_check_credentials(ssid, password) == WIFI_CREDENTIALS_OK) {
        const wifi_config_t config = current_config();
        if (wifi_connect(&wifi, &config) == WIFI_OK) {
            auto_connect = true;
            cli_printf(&cli, "stored credentials found; associating with %s\r\n", ssid);
        }
    }

    wifi_state_t reported = WIFI_STATE_IDLE;

    while (true) {
        cli_poll(&cli);
        wifi_poll(&wifi);

        /* Announce transitions, so an outage and its recovery are visible
           without anyone polling wifistatus. */
        const wifi_state_t state = wifi_state(&wifi);
        if (state != reported) {
            reported = state;
            cli_printf(&cli, "\r\n[wifi] %s", wifi_state_name(state));
            if (state == WIFI_STATE_CONNECTED) {
                cli_printf(&cli, " as %s", wifi_address(&wifi));
            }
            cli_write(&cli, "\r\n");
            cli_write_prompt(&cli);
        }

        sleep_ms(1);
    }
}
