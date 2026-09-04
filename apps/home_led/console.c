#include "console.h"

#include <string.h>

#include "cli_builtins.h"
#include "cli_stream.h"
#include "net.h"
#include "settings.h"

/*
 * Sized for an Intel HEX record, which is up to 521 characters. The console
 * and a firmware transfer share this link, so the buffer has to suit the
 * larger of the two.
 */
static char line_buffer[600];

/*
 * History is sized separately, and much smaller. Eight slots of 48 characters
 * is plenty for typed commands, where a slot the size of `line_buffer` would
 * spend 4.8 KiB of RAM remembering lines nobody typed.
 */
#define HISTORY_ENTRIES 8u
#define HISTORY_ENTRY_SIZE 48u

static char history_buffer[HISTORY_ENTRIES * HISTORY_ENTRY_SIZE];

/* Every set that goes on the table, with room to spare so adding one is not
   also a sizing exercise. */
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + FIRMWARE_SERVICE_MAX_COMMANDS +
                              SETTINGS_COMMAND_COUNT + NET_COMMAND_COUNT +
                              CONSOLE_LIGHT_COMMAND_COUNT + 4u];

/* ---------------------------------------------------------------------------
 * The light
 * -------------------------------------------------------------------------*/

static int cmd_on(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    light_set_power(&app->light, true, app_now_ms());
    cli_write(c, "on\r\n");
    return CLI_OK;
}

static int cmd_off(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    light_set_power(&app->light, false, app_now_ms());
    cli_write(c, "off\r\n");
    return CLI_OK;
}

static int cmd_brightness(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    uint32_t value;

    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "brightness %u (showing %u)\r\n", (unsigned)app->light.brightness,
                   (unsigned)light_current_brightness(&app->light));
        return CLI_OK;
    }
    if (value > 255u) {
        cli_write(c, "0..255\r\n");
        return CLI_ERR_ARG;
    }

    light_set_brightness(&app->light, (uint8_t)value, app_now_ms());
    cli_printf(c, "brightness %u\r\n", (unsigned)value);
    return CLI_OK;
}

static int cmd_rgb(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (!cli_next_u32(c, &r) || !cli_next_u32(c, &g) || !cli_next_u32(c, &b)) {
        cli_printf(c, "usage: rgb <r> <g> <b>  (now %u %u %u)\r\n",
                   (unsigned)app->light.color.r, (unsigned)app->light.color.g,
                   (unsigned)app->light.color.b);
        return CLI_ERR_ARG;
    }
    if (r > 255u || g > 255u || b > 255u) {
        cli_write(c, "each channel is 0..255\r\n");
        return CLI_ERR_ARG;
    }

    light_set_color(&app->light, ws2812_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b),
                    app_now_ms());
    cli_printf(c, "rgb %u %u %u\r\n", (unsigned)r, (unsigned)g, (unsigned)b);
    return CLI_OK;
}

static int cmd_ct(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    uint32_t value;

    if (!cli_next_u32(c, &value)) {
        cli_printf(c, "color temperature %u mireds (showing %u)\r\n",
                   (unsigned)app->light.mireds,
                   (unsigned)light_current_mireds(&app->light));
        return CLI_OK;
    }

    light_set_mireds(&app->light, (uint16_t)(value > 65535u ? 65535u : value),
                     app_now_ms());
    cli_printf(c, "color temperature %u mireds\r\n", (unsigned)app->light.mireds);
    return CLI_OK;
}

static int cmd_effect(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    const char *name = cli_next_token(c);

    if (name == NULL) {
        cli_printf(c, "effect %s\r\n", light_effect_name(app->light.effect));
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

    light_set_effect(&app->light, effect, app_now_ms());
    cli_printf(c, "effect %s\r\n", light_effect_name(effect));
    return CLI_OK;
}

static int cmd_range(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    uint32_t first;
    uint32_t last;

    if (!cli_next_u32(c, &first)) {
        cli_printf(c, "range %u..%u\r\n", (unsigned)led_range_first(&app->range),
                   (unsigned)led_range_last(&app->range));
        return CLI_OK;
    }
    if (!cli_next_u32(c, &last) || first < 1u || first > last ||
        last > APP_LED_COUNT) {
        cli_printf(c, "usage: range <first> <last>, 1..%u\r\n",
                   (unsigned)APP_LED_COUNT);
        return CLI_ERR_ARG;
    }

    (void)led_range_set(&app->range, (uint16_t)first, (uint16_t)last);
    cli_printf(c, "range %u..%u\r\n", (unsigned)first, (unsigned)last);
    return CLI_OK;
}

static int cmd_test(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;

    /* Overrides the effect until switched off again, so wiring can be checked
       without disturbing whatever the light was set to. */
    app->show_test_pattern = !app->show_test_pattern;
    cli_printf(c, "test pattern %s\r\n", app->show_test_pattern ? "on" : "off");
    return CLI_OK;
}

/*
 * Try a different wire order without rebuilding.
 *
 * Identifying an unlabelled strip means trying orders until red is red, and a
 * reflash per attempt makes that a chore instead of a minute's work. The
 * change is live but not stored -- put the answer in the profile once it is
 * known, so a fresh board comes up right.
 */
static int cmd_order(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    const char *name = cli_next_token(c);

    if (name == NULL) {
        cli_printf(c, "order %s (built with %s)\r\n",
                   ws2812_order_name(ws2812_get_order(&app->strip)), APP_LED_ORDER);
        cli_write(c, "  GRB RGB BRG RBG GBR BGR\r\n");
        cli_write(c, "  set a red colour, then try orders until it looks red\r\n");
        return CLI_OK;
    }

    ws2812_order_t order;
    if (!ws2812_order_from_name(name, &order)) {
        cli_printf(c, "no such order: %s\r\n", name);
        return CLI_ERR_ARG;
    }

    ws2812_set_order(&app->strip, order);
    cli_printf(c, "order %s (not saved; put it in the profile)\r\n",
               ws2812_order_name(order));
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    app_t *app = (app_t *)user_data;
    const light_t *light = &app->light;

    cli_printf(c, "strip        %u leds on gpio %u, %s%s\r\n", (unsigned)APP_LED_COUNT,
               (unsigned)APP_LED_PIN, ws2812_order_name(ws2812_get_order(&app->strip)),
               app->strip_ready ? "" : "  (NOT INITIALISED)");
    cli_printf(c, "light        %s, effect %s\r\n", light->on ? "on" : "off",
               light_effect_name(light->effect));
    cli_printf(c, "range        %u..%u (%u leds)\r\n",
               (unsigned)led_range_first(&app->range),
               (unsigned)led_range_last(&app->range),
               (unsigned)led_range_length(&app->range));
    cli_printf(c, "brightness   %u (showing %u)\r\n", (unsigned)light->brightness,
               (unsigned)light_current_brightness(light));
    if (light->color_mode == LIGHT_COLOR_MODE_TEMP) {
        cli_printf(c, "colour       %u mireds (showing %u)\r\n", (unsigned)light->mireds,
                   (unsigned)light_current_mireds(light));
    } else {
        cli_printf(c, "colour       rgb %u %u %u\r\n", (unsigned)light->color.r,
                   (unsigned)light->color.g, (unsigned)light->color.b);
    }
    cli_printf(c, "fading       %s\r\n", light_is_fading(light) ? "yes" : "no");

    cli_printf(c, "wifi         %s", wifi_state_name(wifi_state(&app->wifi)));
    if (wifi_is_connected(&app->wifi)) {
        cli_printf(c, " as %s", wifi_address(&app->wifi));
    }
    cli_printf(c, "\r\nbroker       %s:%s, %s\r\n",
               app->stored.broker_host[0] != '\0' ? app->stored.broker_host : "<unset>",
               app->stored.broker_port, mqtt_state_name(mqtt_state(&app->mqtt)));

    /*
     * The id the topics were actually built from, which is the one that
     * matters. It is only the stored setting until the next boot, and showing
     * that instead makes `status` disagree with the topics printed right
     * under it -- which is confusing at exactly the moment it is being
     * changed.
     */
    cli_printf(c, "device id    %s\r\n",
               app->ha.device_id[0] != '\0' ? app->ha.device_id : "<unusable>");
    if (strcmp(app->ha.device_id, app->stored.device_id) != 0) {
        cli_printf(c, "             %s stored; reboot to use it\r\n",
                   app->stored.device_id);
    }
    cli_printf(c, "topics       %s\r\n             %s\r\n", app->ha.topic_command,
               app->ha.topic_state);
    cli_printf(c, "sessions     %lu, dropped %lu\r\n",
               (unsigned long)mqtt_sessions(&app->mqtt),
               (unsigned long)mqtt_messages_dropped(&app->mqtt));
    cli_printf(c, "unsaved      %s\r\n",
               light->generation != app->saved_generation ||
                       app->range.generation != app->saved_range_generation
                   ? "yes" : "no");
    cli_printf(c, "updater      %s\r\n", app->firmware_ready ? "ready" : "unavailable");
    return CLI_OK;
}

size_t console_light_commands(app_t *app, cli_command_t *out, size_t capacity)
{
    static const cli_command_t table[CONSOLE_LIGHT_COMMAND_COUNT] = {
        { "on",     "switch the light on",                    cmd_on,         NULL },
        { "off",    "switch the light off",                   cmd_off,        NULL },
        { "bri",    "bri [0-255] - brightness, show or set",  cmd_brightness, NULL },
        { "rgb",    "rgb <r> <g> <b> - set an rgb colour",    cmd_rgb,        NULL },
        { "ct",     "ct [mireds] - colour temperature",       cmd_ct,         NULL },
        { "effect", "effect [name] - show, list, or select",  cmd_effect,     NULL },
        { "range",  "range [first last] - active LEDs",       cmd_range,      NULL },
        { "test",   "toggle the wiring test pattern",         cmd_test,       NULL },
        { "order",  "order [GRB|RGB|...] - strip wire order", cmd_order,      NULL },
        { "status", "light, strip, wifi and broker state",    cmd_status,     NULL },
    };

    size_t written = 0;

    for (size_t i = 0; i < CONSOLE_LIGHT_COMMAND_COUNT && written < capacity; i++) {
        out[written] = table[i];
        out[written].user_data = app;
        written++;
    }
    return written;
}

/* ---------------------------------------------------------------------------
 * Assembly
 * -------------------------------------------------------------------------*/

bool console_init(app_t *app)
{
    size_t count = 0;

    /*
     * The firmware updater first, because it is the one set whose commands a
     * transfer script types by name and whose absence would be silent.
     * firmware_service_init() fails only if the flash layout is unusable, in
     * which case the rest of the console is still worth having.
     */
    app->firmware_ready = firmware_service_init(&app->firmware);
    if (app->firmware_ready) {
        count += firmware_service_commands(&app->firmware, &commands[count],
                                           count_of(commands) - count);
    }

    count += cli_builtin_commands(&commands[count], count_of(commands) - count);
    count += console_light_commands(app, &commands[count], count_of(commands) - count);
    count += settings_commands(app, &commands[count], count_of(commands) - count);
    count += net_commands(app, &commands[count], count_of(commands) - count);

    const cli_config_t config = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "led> ",
        .echo = true,
        .enable_help = true,

        /* Claims incoming Intel HEX records for the updater; everything else
           is dispatched as a command. */
        .line_filter = app->firmware_ready ? firmware_service_line_filter : NULL,
        .line_filter_user_data = &app->firmware,

        .history_buffer = history_buffer,
        .history_buffer_size = sizeof(history_buffer),
        .history_entry_size = HISTORY_ENTRY_SIZE,

        /*
         * A HEX record starts with ':'. Marking it raw keeps an upload from
         * being echoed character by character or landing in history, without
         * changing how it is dispatched -- so an interactive session and a
         * firmware transfer can share this console without spoiling each
         * other.
         */
        .raw_line_prefix = ':',
    };

    return cli_init(&app->cli, &config) == CLI_INIT_OK;
}

void console_banner(app_t *app)
{
    cli_printf(&app->cli, "\r\nhome_led  board %s  %u leds on gpio %u  radio %s\r\n",
               PICO_BOARD, (unsigned)APP_LED_COUNT, (unsigned)APP_LED_PIN,
               WIFI_SUPPORTED ? "present" : "none");

    if (!app->strip_ready) {
        cli_write(&app->cli, "the strip did not initialise; check the pio and pin\r\n");
    }
    if (!app->firmware_ready) {
        cli_write(&app->cli, "the firmware updater is unavailable; check the flash layout\r\n");
    }
    cli_write(&app->cli, "type help. Set ssid/password/broker, save, then connect\r\n");
    cli_write_prompt(&app->cli);
}
