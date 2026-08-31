/*
 * config_test - bench for the persistent_config component.
 *
 * Set values, save them, pull the power, and check they come back. That last
 * part is the whole point and is the one thing a host test cannot do.
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

/* The working copy. Sized to a slot, so anything that fits in flash fits
   here — the component never allocates. */
static uint8_t config_buffer[4096u - 256u];
static persistent_config_t config;

static char line_buffer[320];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 8u];

static int cmd_set(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *key = cli_next_token(c);
    const char *value = cli_rest(c);
    if (key == NULL || value == NULL) {
        cli_write(c, "usage: set <key> <value>\r\n");
        return CLI_ERR_ARG;
    }

    const config_result_t result =
        config_set_string(persistent_config_store(&config), key, value);
    if (result != CONFIG_OK) {
        cli_printf(c, "error: %s\r\n", config_result_name(result));
        return CLI_ERR_FAILED;
    }

    cli_write(c, "set (not yet saved)\r\n");
    return CLI_OK;
}

static int cmd_get(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *key = cli_next_token(c);
    if (key == NULL) {
        cli_write(c, "usage: get <key>\r\n");
        return CLI_ERR_ARG;
    }

    char value[256];
    const config_result_t result =
        config_get_string(persistent_config_store(&config), key,
                          value, sizeof(value), "<absent>");
    cli_printf(c, "%s = %s%s\r\n", key, value,
               result == CONFIG_OK ? "" : "  (not stored)");
    return CLI_OK;
}

static int cmd_unset(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *key = cli_next_token(c);
    if (key == NULL) {
        cli_write(c, "usage: unset <key>\r\n");
        return CLI_ERR_ARG;
    }

    const config_result_t result =
        config_remove(persistent_config_store(&config), key);
    cli_printf(c, "%s\r\n", config_result_name(result));
    return result == CONFIG_OK ? CLI_OK : CLI_ERR_FAILED;
}

static int cmd_list(cli_t *c, void *user_data)
{
    (void)user_data;

    config_store_t *store = persistent_config_store(&config);
    const size_t count = config_count(store);

    if (count == 0) {
        cli_write(c, "nothing stored\r\n");
    }
    for (size_t i = 0; i < count; i++) {
        char key[CONFIG_MAX_KEY_LENGTH + 1];
        if (!config_key_at(store, i, key, sizeof(key))) {
            break;
        }
        char value[256];
        config_get_string(store, key, value, sizeof(value), "");
        cli_printf(c, "  %-20s %s\r\n", key, value);
    }

    cli_printf(c, "%u key%s, %u of %u bytes used\r\n", (unsigned)count,
               count == 1 ? "" : "s", (unsigned)config_store_used(store),
               (unsigned)persistent_config_capacity());
    return CLI_OK;
}

static int cmd_save(cli_t *c, void *user_data)
{
    (void)user_data;

    const persistent_config_result_t result = persistent_config_save(&config);
    if (result != PERSISTENT_CONFIG_OK) {
        cli_printf(c, "error: %s\r\n", persistent_config_result_name(result));
        return CLI_ERR_FAILED;
    }

    cli_printf(c, "saved to slot %u, sequence %lu\r\n", config.current,
               (unsigned long)persistent_config_sequence(&config));
    return CLI_OK;
}

static int cmd_load(cli_t *c, void *user_data)
{
    (void)user_data;

    const persistent_config_result_t result =
        persistent_config_load(&config, config_buffer, sizeof(config_buffer));
    cli_printf(c, "%s", persistent_config_result_name(result));
    if (result == PERSISTENT_CONFIG_OK) {
        cli_printf(c, ": slot %u, sequence %lu, %u keys", config.current,
                   (unsigned long)persistent_config_sequence(&config),
                   (unsigned)config_count(persistent_config_store(&config)));
    }
    cli_write(c, "\r\n");
    return CLI_OK;
}

static int cmd_wipe(cli_t *c, void *user_data)
{
    (void)user_data;

    const persistent_config_result_t result = persistent_config_erase(&config);
    cli_printf(c, "%s\r\n", persistent_config_result_name(result));
    return result == PERSISTENT_CONFIG_OK ? CLI_OK : CLI_ERR_FAILED;
}

static int cmd_info(cli_t *c, void *user_data)
{
    (void)user_data;

    const flash_layout_t *layout = flash_layout_get();
    cli_printf(c, "data region   0x%06lX, %lu KiB\r\n",
               (unsigned long)layout->data.offset,
               (unsigned long)(layout->data.size / 1024u));
    cli_printf(c, "slot A        0x%06lX\r\n", (unsigned long)config.slot[0].offset);
    cli_printf(c, "slot B        0x%06lX\r\n", (unsigned long)config.slot[1].offset);
    cli_printf(c, "current slot  %u\r\n", config.current);
    cli_printf(c, "sequence      %lu\r\n",
               (unsigned long)persistent_config_sequence(&config));
    cli_printf(c, "capacity      %u bytes\r\n",
               (unsigned)persistent_config_capacity());
    return CLI_OK;
}

/*
 * Save repeatedly and check the slot alternates, which is what makes an
 * interrupted save survivable and halves the wear on either sector.
 */
static int cmd_churn(cli_t *c, void *user_data)
{
    (void)user_data;

    uint32_t rounds;
    if (!cli_next_u32(c, &rounds) || rounds == 0 || rounds > 100) {
        cli_write(c, "usage: churn <1-100>\r\n");
        return CLI_ERR_ARG;
    }

    uint8_t previous = config.current;
    unsigned alternated = 0;
    unsigned failed = 0;

    for (uint32_t i = 0; i < rounds; i++) {
        config_set_u32(persistent_config_store(&config), "churn", i);
        if (persistent_config_save(&config) != PERSISTENT_CONFIG_OK) {
            failed++;
            continue;
        }
        if (config.current != previous) {
            alternated++;
        }
        previous = config.current;
    }

    cli_printf(c, "%lu saves, %u alternated, %u failed, sequence now %lu\r\n",
               (unsigned long)rounds, alternated, failed,
               (unsigned long)persistent_config_sequence(&config));

    /* Every save should have gone to the other slot. */
    cli_printf(c, "alternation %s\r\n",
               alternated == rounds - failed ? "correct" : "WRONG");
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "set",   "set <key> <value>",                cmd_set,   NULL },
    { "get",   "get <key>",                        cmd_get,   NULL },
    { "unset", "unset <key>",                      cmd_unset, NULL },
    { "list",  "everything stored",                cmd_list,  NULL },
    { "save",  "write to flash",                   cmd_save,  NULL },
    { "load",  "re-read from flash",               cmd_load,  NULL },
    { "wipe",  "erase both slots",                 cmd_wipe,  NULL },
    { "cfginfo", "slot layout and sequence",       cmd_info,  NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const persistent_config_result_t loaded =
        persistent_config_load(&config, config_buffer, sizeof(config_buffer));

    size_t count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0; i < count_of(own_commands) && count < count_of(commands); i++) {
        commands[count++] = own_commands[i];
    }
    /* churn is added separately so the table above stays the documented set. */
    if (count < count_of(commands)) {
        commands[count++] = (cli_command_t){ "churn", "churn <n> - save n times",
                                             cmd_churn, NULL };
    }

    const cli_config_t config_cli = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "cfg> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &config_cli) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\nconfig_test  board %s\r\n", PICO_BOARD);
    cli_printf(&cli, "load: %s", persistent_config_result_name(loaded));
    if (loaded == PERSISTENT_CONFIG_OK) {
        cli_printf(&cli, " (slot %u, sequence %lu, %u keys)", config.current,
                   (unsigned long)persistent_config_sequence(&config),
                   (unsigned)config_count(persistent_config_store(&config)));
    }
    cli_write(&cli, "\r\ntype help\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);
        sleep_ms(1);
    }
}
