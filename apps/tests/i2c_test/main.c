/*
 * i2c_test - interactive bench for the i2c_device component.
 *
 * A serial command line onto an I2C bus: scan it, probe an address, read and
 * write registers. "Is the sensor even wired up" is the first question on a new
 * board, and this answers it without guessing.
 *
 * See README.md.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "cli.h"
#include "cli_builtins.h"
#include "cli_stream.h"
#include "i2c_device.h"

/* Overridable from the profiles under profiles/tests/i2c_test. */
#ifndef I2C_TEST_INSTANCE
#define I2C_TEST_INSTANCE 0
#endif

#ifndef I2C_TEST_SDA_PIN
#define I2C_TEST_SDA_PIN 4
#endif

#ifndef I2C_TEST_SCL_PIN
#define I2C_TEST_SCL_PIN 5
#endif

#ifndef I2C_TEST_BAUDRATE
#define I2C_TEST_BAUDRATE 100000
#endif

#ifndef I2C_TEST_INTERNAL_PULLUPS
#define I2C_TEST_INTERNAL_PULLUPS 1
#endif

static i2c_inst_t *bus;
static char line_buffer[128];
static cli_t cli;
static cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 8u];

/* The address most recently selected, so reads and writes need not repeat it. */
static uint8_t selected_address;
static i2c_device_t device;
static bool device_selected;

static bool take_address(cli_t *c, uint8_t *address)
{
    uint32_t value;
    if (!cli_next_hex32(c, &value) || value < I2C_ADDRESS_MIN || value > I2C_ADDRESS_MAX) {
        cli_printf(c, "address must be 0x%02X..0x%02X (7-bit, hex)\r\n",
                   I2C_ADDRESS_MIN, I2C_ADDRESS_MAX);
        return false;
    }
    *address = (uint8_t)value;
    return true;
}

static bool require_selection(cli_t *c)
{
    if (!device_selected) {
        cli_write(c, "no device selected; use 'use <addr>' first\r\n");
        return false;
    }
    return true;
}

static int cmd_scan(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t found[16];
    const size_t count = i2c_bus_scan(bus, found, count_of(found), 5000);

    if (count == 0) {
        cli_write(c, "nothing answered. Check wiring, power and pull-ups\r\n");
        return CLI_OK;
    }

    cli_printf(c, "%u device%s:", (unsigned)count, count == 1 ? "" : "s");
    for (size_t i = 0; i < count; i++) {
        cli_printf(c, " 0x%02X", found[i]);
    }
    cli_write(c, "\r\n");
    return CLI_OK;
}

static int cmd_use(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t address;
    if (!take_address(c, &address)) {
        return CLI_ERR_ARG;
    }

    /*
     * Byte order is a per-device property and there is no way to detect it, so
     * it is selectable here. Big-endian is the default because nearly every
     * sensor is; getting it wrong gives plausible-looking numbers rather than
     * an error, which is why this prints what it chose.
     */
    i2c_endianness_t endianness = I2C_ENDIAN_BIG;
    const char *order = cli_next_token(c);
    if (order != NULL) {
        if (order[0] == 'l' || order[0] == 'L') {
            endianness = I2C_ENDIAN_LITTLE;
        } else if (order[0] != 'b' && order[0] != 'B') {
            cli_write(c, "byte order must be 'big' or 'little'\r\n");
            return CLI_ERR_ARG;
        }
    }

    const i2c_device_result_t result =
        i2c_device_init(&device, bus, address, endianness, 0);
    if (result != I2C_DEVICE_OK) {
        cli_printf(c, "error: %s\r\n", i2c_device_result_name(result));
        return CLI_ERR_FAILED;
    }

    selected_address = address;
    device_selected = true;

    cli_printf(c, "0x%02X selected, %s-endian, %s\r\n", address,
               endianness == I2C_ENDIAN_BIG ? "big" : "little",
               i2c_device_present(&device) ? "and it answers"
                                           : "but nothing answers there");
    return CLI_OK;
}

static int cmd_probe(cli_t *c, void *user_data)
{
    (void)user_data;

    uint8_t address;
    if (!take_address(c, &address)) {
        return CLI_ERR_ARG;
    }

    i2c_device_t probe;
    i2c_device_init(&probe, bus, address, I2C_ENDIAN_BIG, 0);
    cli_printf(c, "0x%02X %s\r\n", address,
               i2c_device_present(&probe) ? "answers" : "silent");
    return CLI_OK;
}

static int cmd_read(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!require_selection(c)) {
        return CLI_ERR_STATE;
    }

    uint32_t reg, width = 1;
    if (!cli_next_hex32(c, &reg) || reg > 0xFF) {
        cli_write(c, "usage: read <reg> [width 1-4]\r\n");
        return CLI_ERR_ARG;
    }
    if (!cli_args_exhausted(c) && (!cli_next_u32(c, &width) || width < 1 || width > 4)) {
        cli_write(c, "width must be 1 to 4\r\n");
        return CLI_ERR_ARG;
    }

    uint32_t value = 0;
    const i2c_device_result_t result =
        i2c_device_read_value(&device, (uint8_t)reg, (uint8_t)width, &value);
    if (result != I2C_DEVICE_OK) {
        cli_printf(c, "error: %s\r\n", i2c_device_result_name(result));
        return CLI_ERR_FAILED;
    }

    /* Both interpretations, because which is right depends on the register and
       the datasheet is the only thing that knows. */
    cli_printf(c, "0x%02X = %lu (0x%0*lX), as signed %ld\r\n",
               (unsigned)reg, (unsigned long)value, (int)(width * 2u),
               (unsigned long)value, (long)i2c_sign_extend(value, (uint8_t)(width * 8u)));
    return CLI_OK;
}

static int cmd_write(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!require_selection(c)) {
        return CLI_ERR_STATE;
    }

    uint32_t reg, value, width = 1;
    if (!cli_next_hex32(c, &reg) || reg > 0xFF || !cli_next_hex32(c, &value)) {
        cli_write(c, "usage: write <reg> <value> [width 1-4]\r\n");
        return CLI_ERR_ARG;
    }
    if (!cli_args_exhausted(c) && (!cli_next_u32(c, &width) || width < 1 || width > 4)) {
        cli_write(c, "width must be 1 to 4\r\n");
        return CLI_ERR_ARG;
    }

    const i2c_device_result_t result =
        i2c_device_write_value(&device, (uint8_t)reg, (uint8_t)width, value);
    if (result != I2C_DEVICE_OK) {
        cli_printf(c, "error: %s\r\n", i2c_device_result_name(result));
        return CLI_ERR_FAILED;
    }

    cli_write(c, "ok\r\n");
    return CLI_OK;
}

/* A block dump, for getting the shape of an unfamiliar device. */
static int cmd_dump(cli_t *c, void *user_data)
{
    (void)user_data;

    if (!require_selection(c)) {
        return CLI_ERR_STATE;
    }

    uint32_t first = 0, count = 16;
    if (!cli_args_exhausted(c) && !cli_next_hex32(c, &first)) {
        cli_write(c, "usage: dump [first] [count]\r\n");
        return CLI_ERR_ARG;
    }
    if (!cli_args_exhausted(c) && !cli_next_u32(c, &count)) {
        cli_write(c, "usage: dump [first] [count]\r\n");
        return CLI_ERR_ARG;
    }
    if (first > 0xFF || count == 0 || first + count > 0x100) {
        cli_write(c, "range must lie within 0x00..0xFF\r\n");
        return CLI_ERR_RANGE;
    }

    for (uint32_t reg = first; reg < first + count; reg++) {
        uint8_t byte = 0;
        const i2c_device_result_t result =
            i2c_device_read_bytes(&device, (uint8_t)reg, &byte, 1);
        if (result != I2C_DEVICE_OK) {
            cli_printf(c, "0x%02X  --  %s\r\n", (unsigned)reg,
                       i2c_device_result_name(result));
            continue;
        }
        cli_printf(c, "0x%02X  %02X\r\n", (unsigned)reg, byte);
    }
    return CLI_OK;
}

static int cmd_info(cli_t *c, void *user_data)
{
    (void)user_data;
    cli_printf(c, "bus       i2c%d on SDA %d, SCL %d at %d Hz\r\n",
               I2C_TEST_INSTANCE, I2C_TEST_SDA_PIN, I2C_TEST_SCL_PIN,
               I2C_TEST_BAUDRATE);
    cli_printf(c, "pull-ups  %s\r\n",
               I2C_TEST_INTERNAL_PULLUPS ? "internal, enabled" : "external only");
    if (device_selected) {
        cli_printf(c, "selected  0x%02X\r\n", selected_address);
    } else {
        cli_write(c, "selected  none\r\n");
    }
    return CLI_OK;
}

static const cli_command_t own_commands[] = {
    { "scan",  "find every device on the bus",          cmd_scan,  NULL },
    { "probe", "probe <addr> - does anything answer",   cmd_probe, NULL },
    { "use",   "use <addr> [big|little] - select one",  cmd_use,   NULL },
    { "read",  "read <reg> [width]",                    cmd_read,  NULL },
    { "write", "write <reg> <value> [width]",           cmd_write, NULL },
    { "dump",  "dump [first] [count] - a block of regs", cmd_dump, NULL },
    { "info",  "bus configuration",                     cmd_info,  NULL },
};

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    bus = (I2C_TEST_INSTANCE == 1) ? i2c1 : i2c0;

    /* The application owns the bus: several devices share it, so a component
       that claimed it would be wrong. */
    i2c_init(bus, I2C_TEST_BAUDRATE);
    gpio_set_function(I2C_TEST_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_TEST_SCL_PIN, GPIO_FUNC_I2C);
#if I2C_TEST_INTERNAL_PULLUPS
    /*
     * The internal pull-ups are around 50 kohm, which is far weaker than I2C
     * wants. They are enough for a short lead at 100 kHz and no substitute for
     * proper 4.7 kohm resistors on a real board.
     */
    gpio_pull_up(I2C_TEST_SDA_PIN);
    gpio_pull_up(I2C_TEST_SCL_PIN);
#endif

    size_t count = cli_builtin_commands(commands, count_of(commands));
    for (unsigned i = 0; i < count_of(own_commands) && count < count_of(commands); i++) {
        commands[count++] = own_commands[i];
    }

    const cli_config_t config = {
        .commands = commands,
        .command_count = count,
        .stream = cli_stream_stdio(),
        .line_buffer = line_buffer,
        .line_buffer_size = sizeof(line_buffer),
        .prompt = "i2c> ",
        .echo = true,
        .enable_help = true,
    };

    if (cli_init(&cli, &config) != CLI_INIT_OK) {
        while (true) {
            printf("cli_init failed\n");
            sleep_ms(1000);
        }
    }

    cli_printf(&cli, "\r\ni2c_test  i2c%d  SDA %d  SCL %d  %d Hz  board %s\r\n",
               I2C_TEST_INSTANCE, I2C_TEST_SDA_PIN, I2C_TEST_SCL_PIN,
               I2C_TEST_BAUDRATE, PICO_BOARD);
    cli_write(&cli, "type help, or scan to find devices\r\n");
    cli_write_prompt(&cli);

    while (true) {
        cli_poll(&cli);
        sleep_ms(1);
    }
}
