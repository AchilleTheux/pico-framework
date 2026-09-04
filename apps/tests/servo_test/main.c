/*
 * servo_test - interactive bench for the ax12 and feetech components.
 *
 * A serial command line onto a servo bus: scan it, read and write registers by
 * name, move a servo, and watch the bus statistics. This is the tool for
 * bringing a new servo up, not a pass/fail self-test — a servo bus needs a
 * servo, and what you want then is to poke at it.
 *
 * It also shows the point of the component model: this file composes cli,
 * half_duplex_uart, ax12/feetech and optionally ws2812 without knowing how any
 * of them work inside. See README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"

/* Labels this file's output; see log.h. */
#define LOG_TAG "servo"

#include "cli.h"
#include "log.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "half_duplex_uart.h"

/* Overridable from the profiles under profiles/tests/servo_test. */
#ifndef SERVO_TEST_FAMILY_FEETECH
#define SERVO_TEST_FAMILY_FEETECH 0
#endif

#ifndef SERVO_TEST_FEETECH_SCS
#define SERVO_TEST_FEETECH_SCS 0
#endif

#ifndef SERVO_TEST_PIN
#define SERVO_TEST_PIN 21
#endif

#ifndef SERVO_TEST_DIRECTION_PIN
#define SERVO_TEST_DIRECTION_PIN HALF_DUPLEX_UART_NO_DIRECTION_PIN
#endif

#ifndef SERVO_TEST_BAUDRATE
#define SERVO_TEST_BAUDRATE 1000000
#endif

#ifndef SERVO_TEST_STATUS_LEDS
#define SERVO_TEST_STATUS_LEDS 0
#endif

#ifndef SERVO_TEST_LED_PIN
#define SERVO_TEST_LED_PIN 10
#endif

#ifndef SERVO_TEST_LED_COUNT
#define SERVO_TEST_LED_COUNT 8
#endif

#if SERVO_TEST_FAMILY_FEETECH
#include "feetech.h"
#define SERVO_REG_WIDTH(reg)     feetech_register_width(reg)
#define SERVO_REG_NAME(reg)      feetech_register_name(reg)
#define SERVO_REG_IS_EEPROM(reg) feetech_register_is_eeprom(reg)
#define SERVO_POSITION_MAX       FEETECH_POSITION_MAX
#define SERVO_ID_MAX             FEETECH_ID_MAX
#define SERVO_FAMILY_NAME        (SERVO_TEST_FEETECH_SCS ? "feetech SCS" : "feetech STS")
#else
#include "ax12.h"
#define SERVO_REG_WIDTH(reg)     ax12_register_width(reg)
#define SERVO_REG_NAME(reg)      ax12_register_name(reg)
#define SERVO_REG_IS_EEPROM(reg) ax12_register_is_eeprom(reg)
#define SERVO_POSITION_MAX       AX12_POSITION_MAX
#define SERVO_ID_MAX             AX12_ID_MAX
#define SERVO_FAMILY_NAME        "ax12"
#endif

#if SERVO_TEST_STATUS_LEDS
#include "ws2812.h"
static ws2812_color_t led_pixels[SERVO_TEST_LED_COUNT];
static ws2812_strip_t led_strip;
static bool leds_ready;
#endif

static half_duplex_uart_t uart;
static servo_bus_t bus;

/* Keeps the most recent output, so the lines just before something went wrong
   are still there when the console was not being watched. */
static uint8_t log_memory[1024];

static char line_buffer[128];
static cli_t cli;

/* ------------------------------------------------------------------------
 * Family-neutral wrappers, so the commands below read the same either way.
 * ---------------------------------------------------------------------- */

static servo_bus_result_t servo_read_reg(uint8_t id, uint8_t reg, uint32_t *value,
                                         uint8_t *error_out)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_read_register(&bus, id, reg, value, error_out);
#else
    return ax12_read_register(&bus, id, reg, value, error_out);
#endif
}

static servo_bus_result_t servo_write_reg(uint8_t id, uint8_t reg, uint32_t value,
                                          uint8_t *error_out)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_write_register(&bus, id, reg, value, error_out);
#else
    return ax12_write_register(&bus, id, reg, value, error_out);
#endif
}

static servo_bus_result_t servo_scan_bus(uint8_t *ids, size_t capacity, size_t *found)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_scan(&bus, ids, capacity, found);
#else
    return ax12_scan(&bus, ids, capacity, found);
#endif
}

/* The rates this family can actually be set to, for the sweep. */
static const uint32_t *servo_baud_rate_table(size_t *count)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_baud_rate_table(count);
#else
    return ax12_baud_rate_table(count);
#endif
}

static servo_bus_result_t servo_factory_reset(uint8_t id)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_factory_reset(&bus, id);
#else
    return ax12_factory_reset(&bus, id);
#endif
}

static uint8_t servo_default_id(void)
{
#if SERVO_TEST_FAMILY_FEETECH
    return FEETECH_DEFAULT_ID;
#else
    return AX12_DEFAULT_ID;
#endif
}

static uint32_t servo_default_baudrate(void)
{
#if SERVO_TEST_FAMILY_FEETECH
    return FEETECH_DEFAULT_BAUDRATE;
#else
    return AX12_DEFAULT_BAUDRATE;
#endif
}

/* Moves the servo and the host together; see ax12_set_baudrate(). */
static servo_bus_result_t servo_set_servo_baudrate(uint8_t id, uint32_t rate)
{
#if SERVO_TEST_FAMILY_FEETECH
    return feetech_set_baudrate(&bus, id, rate);
#else
    return ax12_set_baudrate(&bus, id, rate);
#endif
}

static uint8_t reg_goal_position(void)
{
#if SERVO_TEST_FAMILY_FEETECH
    return FEETECH_REG_GOAL_POSITION;
#else
    return AX12_REG_GOAL_POSITION;
#endif
}

static uint8_t reg_present_position(void)
{
#if SERVO_TEST_FAMILY_FEETECH
    return FEETECH_REG_PRESENT_POSITION;
#else
    return AX12_REG_PRESENT_POSITION;
#endif
}

static uint8_t reg_torque_enable(void)
{
#if SERVO_TEST_FAMILY_FEETECH
    return FEETECH_REG_TORQUE_ENABLE;
#else
    return AX12_REG_TORQUE_ENABLE;
#endif
}

/* ------------------------------------------------------------------------
 * Status indication
 * ---------------------------------------------------------------------- */

static void show_outcome(bool ok)
{
#if SERVO_TEST_STATUS_LEDS
    if (leds_ready) {
        ws2812_fill(&led_strip, ok ? WS2812_COLOR_GREEN : WS2812_COLOR_RED);
        ws2812_show(&led_strip);
    }
#else
    (void)ok;
#endif
}

/* Report a failed transaction and light the strip red. Returns the CLI code. */
static int fail(cli_t *c, servo_bus_result_t result)
{
    cli_printf(c, "error: %s\r\n", servo_bus_result_name(result));

    /*
     * Also logged, so a failure that happened while nobody was reading the
     * console is still in the memory sink afterwards.
     */
    LOG_WARN("transaction failed: %s", servo_bus_result_name(result));

    show_outcome(false);
    return CLI_ERR_FAILED;
}

static void report_servo_error(cli_t *c, uint8_t error)
{
    if (error != 0) {
        char description[96];
        cli_printf(c, "servo reports: %s\r\n",
                   servo_protocol_describe_error(error, description, sizeof(description)));
    }
}

/* Read one argument that must be a valid servo ID. */
static bool take_id(cli_t *c, uint8_t *id)
{
    uint32_t value;
    if (!cli_next_u32(c, &value) || value > SERVO_ID_MAX) {
        cli_printf(c, "expected an id from 0 to %u\r\n", (unsigned)SERVO_ID_MAX);
        return false;
    }
    *id = (uint8_t)value;
    return true;
}

/*
 * Read a register argument, given either by name or as a number. Names are
 * what makes this usable without the datasheet open.
 */
static bool take_register(cli_t *c, uint8_t *reg)
{
    const char *token = cli_next_token(c);
    if (token == NULL) {
        cli_write(c, "expected a register name or address\r\n");
        return false;
    }

    for (unsigned address = 0; address < 256; address++) {
        const char *name = SERVO_REG_NAME((uint8_t)address);
        if (name != NULL && strcmp(name, token) == 0) {
            *reg = (uint8_t)address;
            return true;
        }
    }

    /* Not a name: accept a number, decimal or 0x-prefixed. */
    char *end = NULL;
    const unsigned long value = strtoul(token, &end, 0);
    if (end != NULL && *end == '\0' && value < 256) {
        *reg = (uint8_t)value;
        return true;
    }

    cli_printf(c, "unknown register: %s   (try 'regs')\r\n", token);
    return false;
}

/* ------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

static int cmd_scan(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t ids[32];
    size_t found = 0;

    cli_printf(c, "scanning ids 0 to %u...\r\n", (unsigned)SERVO_ID_MAX);

    const servo_bus_result_t result = servo_scan_bus(ids, count_of(ids), &found);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    if (found == 0) {
        cli_write(c, "no servos answered\r\n");
        show_outcome(false);
        return CLI_OK;
    }

    cli_printf(c, "%u servo%s:", (unsigned)found, found == 1 ? "" : "s");
    for (size_t i = 0; i < found; i++) {
        cli_printf(c, " %u", ids[i]);
    }
    cli_write(c, "\r\n");
    show_outcome(true);
    return CLI_OK;
}

static int cmd_ping(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    if (!take_id(c, &id)) {
        return CLI_ERR_ARG;
    }

    uint8_t error = 0;
    const servo_bus_result_t result = servo_bus_ping(&bus, id, &error);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    cli_printf(c, "servo %u is there\r\n", id);
    report_servo_error(c, error);
    show_outcome(true);
    return CLI_OK;
}

static int cmd_read(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id, reg;
    if (!take_id(c, &id) || !take_register(c, &reg)) {
        return CLI_ERR_ARG;
    }

    uint32_t value = 0;
    uint8_t error = 0;
    const servo_bus_result_t result = servo_read_reg(id, reg, &value, &error);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    const char *name = SERVO_REG_NAME(reg);
    cli_printf(c, "%s (0x%02X, %u byte%s) = %lu (0x%lX)\r\n",
               name != NULL ? name : "?", reg, SERVO_REG_WIDTH(reg),
               SERVO_REG_WIDTH(reg) == 1 ? "" : "s",
               (unsigned long)value, (unsigned long)value);
    report_servo_error(c, error);
    show_outcome(true);
    return CLI_OK;
}

static int cmd_write(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id, reg;
    uint32_t value;
    if (!take_id(c, &id) || !take_register(c, &reg) || !cli_next_u32(c, &value)) {
        cli_write(c, "usage: write <id> <register> <value>\r\n");
        return CLI_ERR_ARG;
    }

    if (SERVO_REG_IS_EEPROM(reg)) {
        /* EEPROM has limited write endurance, and an ID or baud change takes
           effect immediately — worth saying out loud on a bench tool. */
        cli_write(c, "note: EEPROM register, limited write endurance\r\n");
    }

    uint8_t error = 0;
    const servo_bus_result_t result = servo_write_reg(id, reg, value, &error);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    cli_write(c, "ok\r\n");
    report_servo_error(c, error);
    show_outcome(true);
    return CLI_OK;
}

/* Read the position with no argument, set it with one. */
static int cmd_pos(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    if (!take_id(c, &id)) {
        return CLI_ERR_ARG;
    }

    uint32_t position;
    if (cli_args_exhausted(c)) {
        uint32_t value = 0;
        const servo_bus_result_t result =
            servo_read_reg(id, reg_present_position(), &value, NULL);
        if (result != SERVO_BUS_OK) {
            return fail(c, result);
        }
        cli_printf(c, "servo %u at %lu\r\n", id, (unsigned long)value);
        show_outcome(true);
        return CLI_OK;
    }

    if (!cli_next_u32(c, &position) || position > SERVO_POSITION_MAX) {
        cli_printf(c, "position must be 0 to %u\r\n", (unsigned)SERVO_POSITION_MAX);
        return CLI_ERR_RANGE;
    }

    const servo_bus_result_t result =
        servo_write_reg(id, reg_goal_position(), position, NULL);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    cli_printf(c, "servo %u going to %lu\r\n", id, (unsigned long)position);
    show_outcome(true);
    return CLI_OK;
}

static int cmd_torque(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    uint32_t enable;
    if (!take_id(c, &id) || !cli_next_u32(c, &enable)) {
        cli_write(c, "usage: torque <id> <0|1>\r\n");
        return CLI_ERR_ARG;
    }

    const servo_bus_result_t result =
        servo_write_reg(id, reg_torque_enable(), enable ? 1u : 0u, NULL);
    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    cli_printf(c, "servo %u torque %s\r\n", id, enable ? "on" : "off");
    show_outcome(true);
    return CLI_OK;
}

static int cmd_status(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    if (!take_id(c, &id)) {
        return CLI_ERR_ARG;
    }

#if SERVO_TEST_FAMILY_FEETECH
    uint16_t position = 0;
    int16_t speed = 0, load = 0;
    uint8_t temperature = 0;
    uint32_t millivolts = 0, millidegrees = 0;
    bool moving = false;

    servo_bus_result_t result = feetech_get_present_position(&bus, id, &position);
    if (result == SERVO_BUS_OK) result = feetech_get_present_millidegrees(&bus, id, &millidegrees);
    if (result == SERVO_BUS_OK) result = feetech_get_present_speed(&bus, id, &speed);
    if (result == SERVO_BUS_OK) result = feetech_get_present_load(&bus, id, &load);
    if (result == SERVO_BUS_OK) result = feetech_get_temperature(&bus, id, &temperature);
    if (result == SERVO_BUS_OK) result = feetech_get_voltage_millivolts(&bus, id, &millivolts);
    if (result == SERVO_BUS_OK) result = feetech_is_moving(&bus, id, &moving);
#else
    uint16_t position = 0;
    int16_t speed = 0, load = 0;
    uint8_t temperature = 0;
    uint32_t millivolts = 0, millidegrees = 0;
    bool moving = false;

    servo_bus_result_t result = ax12_get_present_position(&bus, id, &position);
    if (result == SERVO_BUS_OK) result = ax12_get_present_millidegrees(&bus, id, &millidegrees);
    if (result == SERVO_BUS_OK) result = ax12_get_present_speed(&bus, id, &speed);
    if (result == SERVO_BUS_OK) result = ax12_get_present_load(&bus, id, &load);
    if (result == SERVO_BUS_OK) result = ax12_get_temperature(&bus, id, &temperature);
    if (result == SERVO_BUS_OK) result = ax12_get_voltage_millivolts(&bus, id, &millivolts);
    if (result == SERVO_BUS_OK) result = ax12_is_moving(&bus, id, &moving);
#endif

    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    cli_printf(c, "servo %u\r\n", id);
    cli_printf(c, "  position     %u  (%lu.%03lu deg)\r\n", position,
               (unsigned long)(millidegrees / 1000), (unsigned long)(millidegrees % 1000));
    cli_printf(c, "  speed        %d\r\n", speed);
    cli_printf(c, "  load         %d\r\n", load);
    cli_printf(c, "  temperature  %u C\r\n", temperature);
    cli_printf(c, "  voltage      %lu.%lu V\r\n",
               (unsigned long)(millivolts / 1000), (unsigned long)((millivolts % 1000) / 100));
    cli_printf(c, "  moving       %s\r\n", moving ? "yes" : "no");
    show_outcome(true);
    return CLI_OK;
}

static int cmd_regs(cli_t *c, void *user_data)
{
    (void)user_data;

    cli_printf(c, "%s control table:\r\n", SERVO_FAMILY_NAME);
    for (unsigned address = 0; address < 256; address++) {
        const char *name = SERVO_REG_NAME((uint8_t)address);
        if (name != NULL) {
            cli_printf(c, "  0x%02X  %u  %-22s %s\r\n", address,
                       SERVO_REG_WIDTH((uint8_t)address), name,
                       SERVO_REG_IS_EEPROM((uint8_t)address) ? "eeprom" : "");
        }
    }
    return CLI_OK;
}

static int cmd_stats(cli_t *c, void *user_data)
{
    (void)user_data;

    const servo_bus_stats_t *stats = servo_bus_get_stats(&bus);
    const half_duplex_uart_timing_t *timing = half_duplex_uart_get_timing(&uart);

    cli_printf(c, "bus %s at %lu baud (actual %lu, error %ld/1000)\r\n",
               SERVO_FAMILY_NAME, (unsigned long)uart.baudrate,
               (unsigned long)timing->actual_baudrate, (long)timing->error_permille);
    cli_printf(c, "  transactions    %lu\r\n", (unsigned long)stats->transactions);
    cli_printf(c, "  retries         %lu\r\n", (unsigned long)stats->retries);
    cli_printf(c, "  timeouts        %lu\r\n", (unsigned long)stats->timeouts);
    cli_printf(c, "  checksum errors %lu\r\n", (unsigned long)stats->checksum_errors);
    cli_printf(c, "  malformed       %lu\r\n", (unsigned long)stats->malformed);
    cli_printf(c, "  wrong id        %lu\r\n", (unsigned long)stats->wrong_id);
    return CLI_OK;
}

static int cmd_stats_reset(cli_t *c, void *user_data)
{
    (void)user_data;
    servo_bus_reset_stats(&bus);
    cli_write(c, "counters cleared\r\n");
    return CLI_OK;
}

static int cmd_baud(cli_t *c, void *user_data)
{
    (void)user_data;

    uint32_t rate;
    if (!cli_next_u32(c, &rate)) {
        cli_write(c, "usage: baud <rate>\r\n");
        return CLI_ERR_ARG;
    }

    const half_duplex_uart_result_t result = half_duplex_uart_set_baudrate(&uart, rate);
    if (result != HALF_DUPLEX_UART_OK) {
        cli_printf(c, "cannot run at %lu baud: within 2%% is not reachable\r\n",
                   (unsigned long)rate);
        return CLI_ERR_RANGE;
    }

    const half_duplex_uart_timing_t *timing = half_duplex_uart_get_timing(&uart);
    cli_printf(c, "bus now at %lu baud (error %ld/1000)\r\n",
               (unsigned long)timing->actual_baudrate, (long)timing->error_permille);
    return CLI_OK;
}

/*
 * Find servos whose baud rate is unknown, by trying every rate the family
 * supports and pinging at each.
 *
 * This is the command for a servo that has been used before: a second-hand
 * servo answers nothing at all until the host happens to be clocked at the
 * rate somebody left in its EEPROM, and `scan` alone cannot tell that apart
 * from a wiring fault or no power.
 */
static int cmd_discover(cli_t *c, void *user_data)
{
    (void)user_data;

    /* An optional id turns a ten-second sweep into one ping per rate, which is
       what you want when there is one servo on the bench. */
    uint8_t only_id = 0;
    bool single = false;
    if (!cli_args_exhausted(c)) {
        if (!take_id(c, &only_id)) {
            return CLI_ERR_ARG;
        }
        single = true;
    }

    size_t rate_count = 0;
    const uint32_t *rates = servo_baud_rate_table(&rate_count);

    const uint32_t original = servo_bus_get_baudrate(&bus);
    uint32_t only_rate_found = 0;
    unsigned rates_with_servos = 0;
    unsigned servos_found = 0;

    if (single) {
        cli_printf(c, "looking for id %u at %u rates...\r\n", only_id,
                   (unsigned)rate_count);
    } else {
        cli_printf(c, "sweeping %u rates x %u ids; this takes a few seconds\r\n",
                   (unsigned)rate_count, (unsigned)SERVO_ID_MAX + 1u);
    }

    for (size_t i = 0; i < rate_count; i++) {
        if (servo_bus_set_baudrate(&bus, rates[i]) != SERVO_BUS_OK) {
            /* Reachable for the servo but not for this host clock. */
            cli_printf(c, "%9lu  host cannot be clocked here\r\n",
                       (unsigned long)rates[i]);
            continue;
        }

        if (single) {
            const bool there = servo_bus_ping(&bus, only_id, NULL) == SERVO_BUS_OK;
            cli_printf(c, "%9lu  %s\r\n", (unsigned long)rates[i],
                       there ? "answers" : "-");
            if (there) {
                rates_with_servos++;
                only_rate_found = rates[i];
                servos_found = 1;
            }
            continue;
        }

        uint8_t ids[32];
        size_t found = 0;
        if (servo_scan_bus(ids, count_of(ids), &found) != SERVO_BUS_OK || found == 0) {
            cli_printf(c, "%9lu  -\r\n", (unsigned long)rates[i]);
            continue;
        }

        cli_printf(c, "%9lu  %u servo%s:", (unsigned long)rates[i], (unsigned)found,
                   found == 1 ? "" : "s");
        for (size_t j = 0; j < found; j++) {
            cli_printf(c, " %u", ids[j]);
        }
        cli_write(c, "\r\n");

        rates_with_servos++;
        only_rate_found = rates[i];
        servos_found += (unsigned)found;
    }

    /*
     * Where to leave the bus. One rate answered is the ordinary case and the
     * next command is going to be addressed to those servos, so stay there.
     * Two rates means a mixed bus and no single right answer, so go back to
     * where we started rather than picking for the user.
     */
    if (rates_with_servos == 1) {
        (void)servo_bus_set_baudrate(&bus, only_rate_found);
        cli_printf(c, "%u servo%s at %lu baud; bus left there\r\n", servos_found,
                   servos_found == 1 ? "" : "s", (unsigned long)only_rate_found);
    } else {
        (void)servo_bus_set_baudrate(&bus, original);
        if (rates_with_servos == 0) {
            cli_printf(c, "nothing answered at any rate; bus back at %lu baud\r\n",
                       (unsigned long)original);
            cli_write(c, "check power and wiring, not the rate\r\n");
        } else {
            cli_printf(c, "servos at %u different rates; bus back at %lu baud\r\n",
                       rates_with_servos, (unsigned long)original);
        }
    }

    /* Nothing found is a result, not an error: `scan` reports it the same way,
       and the point of this command is to distinguish the causes. */
    show_outcome(rates_with_servos > 0);
    return CLI_OK;
}

/*
 * Change a servo's own baud rate, and follow it with the host.
 *
 * Not to be confused with `baud`, which moves only this end.
 */
static int cmd_setbaud(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *token = cli_next_token(c);
    uint32_t rate = 0;

    if (token == NULL || !cli_next_u32(c, &rate)) {
        cli_write(c, "usage: setbaud <id|all> <rate>\r\n");
        return CLI_ERR_ARG;
    }

    uint8_t id;
    if (strcmp(token, "all") == 0) {
        /* Nothing acknowledges a broadcast, so this cannot be verified — but
           it is the only way to move a populated bus without orphaning
           everything but the first servo. */
        id = SERVO_PROTOCOL_BROADCAST_ID;
    } else {
        char *end = NULL;
        const unsigned long value = strtoul(token, &end, 0);
        if (end == NULL || *end != '\0' || value > SERVO_ID_MAX) {
            cli_printf(c, "expected an id from 0 to %u, or 'all'\r\n",
                       (unsigned)SERVO_ID_MAX);
            return CLI_ERR_ARG;
        }
        id = (uint8_t)value;
    }

    const servo_bus_result_t result = servo_set_servo_baudrate(id, rate);
    if (result == SERVO_BUS_ERR_INVALID_ARG) {
        cli_printf(c, "%lu baud is not a rate both ends can run at\r\n",
                   (unsigned long)rate);
        cli_write(c, "the rates this family offers:");
        size_t rate_count = 0;
        const uint32_t *rates = servo_baud_rate_table(&rate_count);
        for (size_t i = 0; i < rate_count; i++) {
            cli_printf(c, " %lu", (unsigned long)rates[i]);
        }
        cli_write(c, "\r\n");
        return CLI_ERR_RANGE;
    }
    if (result != SERVO_BUS_OK) {
        cli_printf(c, "no answer at %lu baud; bus back at %lu baud\r\n",
                   (unsigned long)rate, (unsigned long)servo_bus_get_baudrate(&bus));
        return fail(c, result);
    }

    const uint32_t now = servo_bus_get_baudrate(&bus);
    if (id == SERVO_PROTOCOL_BROADCAST_ID) {
        cli_printf(c, "sent to every servo; bus now at %lu baud (not acknowledged, "
                      "run scan)\r\n", (unsigned long)now);
    } else {
        /* The exact rate, which for a datasheet name is not the number the
           user typed: "115200" on an AX-12 really is 117647. */
        cli_printf(c, "servo %u and the bus are now at %lu baud\r\n", id,
                   (unsigned long)now);
    }
    show_outcome(true);
    return CLI_OK;
}

/*
 * Put one servo back to its factory settings.
 *
 * Guarded twice, because the reset takes the ID with it and there is no undo:
 * the word `confirm` has to be typed, and the bus has to have exactly one
 * servo on it. Two servos reset on one bus both answer as id 1 afterwards,
 * which is a worse position than whatever the reset was meant to fix.
 */
static int cmd_reset(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    if (!take_id(c, &id)) {
        return CLI_ERR_ARG;
    }

    const char *confirmation = cli_next_token(c);
    if (confirmation == NULL || strcmp(confirmation, "confirm") != 0) {
        cli_printf(c, "this erases servo %u's whole EEPROM: id, baud rate, angle "
                      "limits, everything\r\n", id);
        cli_printf(c, "it will come back as id %u at %lu baud\r\n",
                   servo_default_id(), (unsigned long)servo_default_baudrate());
        cli_printf(c, "type: reset %u confirm\r\n", id);
        return CLI_ERR_ARG;
    }

    /* One servo on the bus, or the reset makes an ID collision. */
    uint8_t ids[8];
    size_t found = 0;
    if (servo_scan_bus(ids, count_of(ids), &found) != SERVO_BUS_OK) {
        cli_write(c, "could not scan the bus first; not resetting anything\r\n");
        return CLI_ERR_FAILED;
    }
    if (found == 0) {
        cli_printf(c, "nothing answers on this bus at %lu baud; try discover\r\n",
                   (unsigned long)servo_bus_get_baudrate(&bus));
        return CLI_ERR_STATE;
    }
    if (found > 1) {
        cli_printf(c, "%u servos answer; reset needs one on the bus, or they all "
                      "come back as id %u\r\n", (unsigned)found, servo_default_id());
        return CLI_ERR_STATE;
    }
    if (ids[0] != id) {
        cli_printf(c, "the one servo here is id %u, not %u\r\n", ids[0], id);
        return CLI_ERR_STATE;
    }

    cli_printf(c, "resetting servo %u and waiting for it to come back...\r\n", id);

    const servo_bus_result_t result = servo_factory_reset(id);
    if (result != SERVO_BUS_OK) {
        cli_printf(c, "it did not answer as id %u at %lu baud; bus back at %lu "
                      "baud\r\n", servo_default_id(),
                   (unsigned long)servo_default_baudrate(),
                   (unsigned long)servo_bus_get_baudrate(&bus));
        cli_write(c, "run discover: a reset that half happened leaves it "
                     "somewhere else\r\n");
        return fail(c, result);
    }

    cli_printf(c, "servo is now id %u at %lu baud; bus left there\r\n",
               servo_default_id(), (unsigned long)servo_bus_get_baudrate(&bus));
    show_outcome(true);
    return CLI_OK;
}

/* Read one register from one servo repeatedly, to see how the link holds up. */
static int cmd_soak(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t id;
    uint32_t iterations;
    if (!take_id(c, &id) || !cli_next_u32(c, &iterations) || iterations == 0) {
        cli_write(c, "usage: soak <id> <iterations>\r\n");
        return CLI_ERR_ARG;
    }

    servo_bus_reset_stats(&bus);

    uint32_t failures = 0;
    for (uint32_t i = 0; i < iterations; i++) {
        uint32_t value = 0;
        if (servo_read_reg(id, reg_present_position(), &value, NULL) != SERVO_BUS_OK) {
            failures++;
        }
    }

    const servo_bus_stats_t *stats = servo_bus_get_stats(&bus);
    cli_printf(c, "%lu reads, %lu failed after retries, %lu retries used\r\n",
               (unsigned long)iterations, (unsigned long)failures,
               (unsigned long)stats->retries);
    show_outcome(failures == 0);
    return CLI_OK;
}

static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 20u];

/* Move several servos at once, so the timing difference is visible: separate
   writes stagger the starts, one sync-write does not. */
static int cmd_sync(cli_t *c, void *user_data)
{
    (void)user_data;

    servo_sync_target_t targets[16];
    uint8_t count = 0;

    while (count < count_of(targets) && !cli_args_exhausted(c)) {
        uint32_t id, position;
        if (!cli_next_u32(c, &id) || !cli_next_u32(c, &position)) {
            cli_write(c, "usage: sync <id> <pos> [<id> <pos> ...]\r\n");
            return CLI_ERR_ARG;
        }
        if (id > SERVO_ID_MAX || position > SERVO_POSITION_MAX) {
            cli_printf(c, "id must be 0..%u and position 0..%u\r\n",
                       (unsigned)SERVO_ID_MAX, (unsigned)SERVO_POSITION_MAX);
            return CLI_ERR_RANGE;
        }
        targets[count].id = (uint8_t)id;
        targets[count].value = position;
        count++;
    }

    if (count == 0) {
        cli_write(c, "usage: sync <id> <pos> [<id> <pos> ...]\r\n");
        return CLI_ERR_ARG;
    }

#if SERVO_TEST_FAMILY_FEETECH
    const servo_bus_result_t result =
        feetech_sync_set_goal_positions(&bus, targets, count);
#else
    const servo_bus_result_t result = ax12_sync_set_goal_positions(&bus, targets, count);
#endif

    if (result != SERVO_BUS_OK) {
        return fail(c, result);
    }

    /* Nothing acknowledges a sync-write, so this only means it went out. */
    cli_printf(c, "sent to %u servos (not acknowledged; read positions to check)\r\n",
               count);
    show_outcome(true);
    return CLI_OK;
}

static int cmd_log(cli_t *c, void *user_data)
{
    (void)user_data;

    const char *wanted = cli_next_token(c);
    if (wanted == NULL) {
        cli_printf(c, "level %s\r\n", log_level_name(log_get_level()));
        return CLI_OK;
    }

    log_level_t level;
    if (!log_level_parse(wanted, &level)) {
        cli_write(c, "level must be trace, debug, info, warn, error or none\r\n");
        return CLI_ERR_ARG;
    }

    log_set_level(level);
    cli_printf(c, "level now %s\r\n", log_level_name(level));
    LOG_INFO("log level set to %s", log_level_name(level));
    return CLI_OK;
}

static int cmd_log_dump(cli_t *c, void *user_data)
{
    (void)user_data;

    char chunk[256];
    size_t total = 0;
    size_t got;

    /* Drains the buffer, which is what you want after reading it once. */
    while ((got = log_read_memory(chunk, sizeof(chunk))) > 0) {
        cli_write_bytes(c, chunk, got);
        total += got;
    }

    cli_printf(c, "\r\n%u bytes\r\n", (unsigned)total);
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "scan",   "find every servo on the bus",        cmd_scan,        NULL },
    { "discover", "discover [id] - sweep every baud rate", cmd_discover,  NULL },
    { "ping",   "ping <id>",                          cmd_ping,        NULL },
    { "read",   "read <id> <register>",               cmd_read,        NULL },
    { "write",  "write <id> <register> <value>",      cmd_write,       NULL },
    { "pos",    "pos <id> [position] - get or set",   cmd_pos,         NULL },
    { "sync",   "sync <id> <pos> ... - all at once",  cmd_sync,        NULL },
    { "log",    "log [level] - show or set the level", cmd_log,         NULL },
    { "logdump", "the recent log held in memory",      cmd_log_dump,    NULL },
    { "torque", "torque <id> <0|1>",                  cmd_torque,      NULL },
    { "status", "status <id> - everything at once",   cmd_status,      NULL },
    { "regs",   "list the control table",             cmd_regs,        NULL },
    { "stats",  "bus health counters",                cmd_stats,       NULL },
    { "clear",  "reset the counters",                 cmd_stats_reset, NULL },
    { "baud",   "baud <rate> - change bus speed",     cmd_baud,        NULL },
    { "setbaud", "setbaud <id|all> <rate> - servo and bus", cmd_setbaud,  NULL },
    { "reset",  "reset <id> confirm - factory settings", cmd_reset,      NULL },
    { "soak",   "soak <id> <n> - hammer the link",    cmd_soak,        NULL },
};

/* ------------------------------------------------------------------------ */

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const half_duplex_uart_config_t uart_config = {
        .pio = pio0,
        .pin = SERVO_TEST_PIN,
        .direction_pin = SERVO_TEST_DIRECTION_PIN,
        .baudrate = SERVO_TEST_BAUDRATE,
        /* A servo bus shares one pad, with or without a level shifter, so we
           hear our own bytes and must consume them. This is also the default. */
        .echo = HALF_DUPLEX_UART_ECHO_DISCARD,
    };

    log_init(LOG_LEVEL_INFO);
    log_add_stdio_sink(LOG_LEVEL_INFO);
    log_add_memory_sink(log_memory, sizeof(log_memory), LOG_LEVEL_DEBUG);

    const half_duplex_uart_result_t uart_result =
        half_duplex_uart_init(&uart, &uart_config);
    if (uart_result != HALF_DUPLEX_UART_OK) {
        while (true) {
            printf("half_duplex_uart_init failed: %d\n", (int)uart_result);
            sleep_ms(1000);
        }
    }

#if SERVO_TEST_FAMILY_FEETECH
    feetech_bus_init(&bus, &uart,
                     SERVO_TEST_FEETECH_SCS ? FEETECH_MODEL_SCS : FEETECH_MODEL_STS);
#else
    ax12_bus_init(&bus, &uart);
#endif

#if SERVO_TEST_STATUS_LEDS
    const ws2812_config_t led_config = {
        .pio = pio1,   /* the servo bus already has two state machines on pio0 */
        .pin = SERVO_TEST_LED_PIN,
        .pixels = led_pixels,
        .length = SERVO_TEST_LED_COUNT,
        .is_rgbw = false,
    };
    leds_ready = (ws2812_init(&led_strip, &led_config) == WS2812_OK);
    if (leds_ready) {
        ws2812_set_brightness(&led_strip, 32);
        ws2812_fill(&led_strip, WS2812_COLOR_BLUE);
        ws2812_show(&led_strip);
    }
#endif

    size_t command_count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0;
         i < count_of(own_commands) && command_count < count_of(commands); i++) {
        commands[command_count++] = own_commands[i];
    }

    const cli_config_t cli_config = {
        .commands = commands,
        .command_count = command_count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "servo> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &cli_config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    const half_duplex_uart_timing_t *timing = half_duplex_uart_get_timing(&uart);
    cli_printf(&cli, "\r\nservo_test - %s on pin %d at %lu baud (error %ld/1000)\r\n",
               SERVO_FAMILY_NAME, SERVO_TEST_PIN,
               (unsigned long)timing->actual_baudrate, (long)timing->error_permille);
    cli_write(&cli, "type help, scan to find servos, or discover if the rate "
                    "is unknown\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);
        sleep_ms(1);
    }
}
