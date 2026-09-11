#include <string.h>
#include "hardware/gpio.h"
#include "pico/time.h"
#include "mcp2518fd.h"
#include "mcp2518fd_timing.h"

/* Microchip DS20006027B; errata DS80000789F. FIFO1 TX, FIFO2 RX.
   TEF and TXQ disabled. The active objects occupy 20 * 72 = 1440 bytes.
   Even the 29 unused FIFOs' reset allocations (16 bytes each) fit in 2 KB. */
#define REG_CON       0x000u
#define REG_NBTCFG    0x004u
#define REG_DBTCFG    0x008u
#define REG_TDC       0x00cu
#define REG_INT       0x01cu
#define REG_TREC      0x034u
#define REG_TXCON     0x05cu
#define REG_TXSTA     0x060u
#define REG_TXUA      0x064u
#define REG_RXCON     0x068u
#define REG_RXSTA     0x06cu
#define REG_RXUA      0x070u
#define REG_FLTCON    0x1d0u
#define REG_FLTOBJ    0x1f0u
#define REG_OSC       0xe00u
#define REG_ECCCON    0xe0cu
#define REG_ECCSTAT   0xe10u
#define RAM_BASE      0x400u
#define RAM_SIZE      2048u
#define MODE_CONFIG   4u
#define CON_CONFIG    ((MODE_CONFIG << 24) | (1u << 5)) /* ISO CRC, no TEF/TXQ */
#define FIFO_RESET    (1u << 10)

static void write_bytes(mcp2518fd_bus_t *bus, uint16_t address, const uint8_t *data, size_t length)
{
    const uint8_t header[] = {(uint8_t)(0x20u | (address >> 8)), (uint8_t)address};
    gpio_put(bus->cs_pin, 0);
    spi_write_blocking(bus->spi, header, sizeof(header));
    spi_write_blocking(bus->spi, data, length);
    gpio_put(bus->cs_pin, 1);
}

static void write_byte(mcp2518fd_bus_t *bus, uint16_t address, uint8_t value)
{
    write_bytes(bus, address, &value, 1);
}

static void write_word(mcp2518fd_bus_t *bus, uint16_t address, uint32_t value)
{
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i++) bytes[i] = (uint8_t)(value >> (8 * i));
    write_bytes(bus, address, bytes, sizeof(bytes));
}

/* All reads use READ_CRC, including status registers (erratum 1).
   N is bytes for registers but WORDS for RAM. Callers use aligned RAM
   transfers and never cross an SFR or RAM region boundary. */
static bool read_bytes(mcp2518fd_bus_t *bus, uint16_t address, uint8_t *data, size_t length)
{
    const bool ram = address >= RAM_BASE && address < RAM_BASE + RAM_SIZE;
    const uint8_t header[] = {
        (uint8_t)(0xb0u | (address >> 8)), (uint8_t)address,
        (uint8_t)(ram ? length / 4 : length),
    };
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        uint8_t crc_bytes[2];
        gpio_put(bus->cs_pin, 0);
        spi_write_blocking(bus->spi, header, sizeof(header));
        spi_read_blocking(bus->spi, 0, data, length);
        spi_read_blocking(bus->spi, 0, crc_bytes, sizeof(crc_bytes));
        gpio_put(bus->cs_pin, 1);
        uint16_t crc = mcp2518fd_crc16(0xffffu, header, sizeof(header));
        crc = mcp2518fd_crc16(crc, data, length);
        if (crc_bytes[0] == (uint8_t)(crc >> 8) && crc_bytes[1] == (uint8_t)crc) return true;
        bus->stats.spi_crc_errors++;
    }
    return false;
}

static bool read_word(mcp2518fd_bus_t *bus, uint16_t address, uint32_t *value)
{
    uint8_t bytes[4];
    if (!read_bytes(bus, address, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

static mcp2518fd_result_t wait_bits(mcp2518fd_bus_t *bus, uint16_t address, uint32_t mask, uint32_t value)
{
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        uint32_t actual;
        if (!read_word(bus, address, &actual)) return MCP2518FD_ERR_SPI;
        if ((actual & mask) == value) return MCP2518FD_OK;
        sleep_us(100);
    }
    return MCP2518FD_ERR_TIMEOUT;
}

static mcp2518fd_result_t switch_mode(mcp2518fd_bus_t *bus, uint8_t mode)
{
    /* Only the REQOP byte: do not write back live CiCON status bits. */
    write_byte(bus, REG_CON + 3, mode);
    return wait_bits(bus, REG_CON, 7u << 21, (uint32_t)mode << 21);
}

static uint8_t mode_code(const mcp2518fd_bus_t *bus, mcp2518fd_mode_t mode)
{
    if (mode == MCP2518FD_MODE_LOOPBACK) return 2;
    if (mode == MCP2518FD_MODE_LISTEN_ONLY) return 3;
    return bus->fd_enabled ? 0 : 6;
}

static bool valid_mode(mcp2518fd_mode_t mode)
{
    return mode == MCP2518FD_MODE_NORMAL || mode == MCP2518FD_MODE_LOOPBACK ||
           mode == MCP2518FD_MODE_LISTEN_ONLY;
}

static void release_pins(mcp2518fd_bus_t *bus)
{
    gpio_put(bus->cs_pin, 1);
    gpio_deinit(bus->cs_pin);
    if (bus->int_pin != MCP2518FD_NO_INT_PIN) gpio_deinit((uint)bus->int_pin);
    bus->initialised = false;
}

static mcp2518fd_result_t check_ram(mcp2518fd_bus_t *bus)
{
    if (bus->ram_fault) return MCP2518FD_ERR_RAM;
    uint32_t ecc;
    if (!read_word(bus, REG_ECCSTAT, &ecc)) return MCP2518FD_ERR_SPI;
    if (ecc & 6u) {
        /* Erratum 2: even a "corrected" single-bit error is untrustworthy.
           Latch the fault until deinit/init, retaining its address. */
        bus->stats.eccstat = ecc;
        bus->ram_fault = true;
        return MCP2518FD_ERR_RAM;
    }
    return MCP2518FD_OK;
}

static mcp2518fd_result_t fifo_address(mcp2518fd_bus_t *bus, uint16_t reg, uint16_t *address)
{
    uint32_t offset;
    if (!read_word(bus, reg, &offset)) return MCP2518FD_ERR_SPI;
    /* UA is relative to 0x400. Validate the expected FIFO's allocation too,
       so a corrupt pointer cannot overwrite another FIFO or an SFR. */
    uint32_t start = reg == REG_TXUA ? 0 : MCP2518FD_TX_DEPTH * MCP2518FD_OBJECT_SIZE;
    uint32_t count = reg == REG_TXUA ? MCP2518FD_TX_DEPTH : MCP2518FD_RX_DEPTH;
    if (offset < start || offset >= start + count * MCP2518FD_OBJECT_SIZE ||
        (offset - start) % MCP2518FD_OBJECT_SIZE != 0) {
        bus->ram_fault = true;
        return MCP2518FD_ERR_RAM;
    }
    *address = (uint16_t)(RAM_BASE + offset);
    return MCP2518FD_OK;
}

mcp2518fd_result_t mcp2518fd_bus_init(mcp2518fd_bus_t *bus, const mcp2518fd_bus_config_t *config)
{
    if (bus == NULL || config == NULL || (config->spi != spi0 && config->spi != spi1) ||
        config->cs_pin >= NUM_BANK0_GPIOS || config->int_pin < MCP2518FD_NO_INT_PIN ||
        config->int_pin >= (int)NUM_BANK0_GPIOS || config->int_pin == (int)config->cs_pin ||
        !valid_mode(config->mode) || config->filter_count > MCP2518FD_MAX_FILTERS ||
        (config->filter_count != 0 && config->filters == NULL) ||
        (!config->fd_enabled && config->data_bitrate != 0)) return MCP2518FD_ERR_INVALID_ARG;
    uint32_t clock = config->oscillator_hz;
    if (clock == 4000000u) clock = 40000000u;
    uint32_t bitrate = config->bitrate ? config->bitrate : MCP2518FD_DEFAULT_BITRATE;
    uint32_t data_bitrate = config->fd_enabled ?
        (config->data_bitrate ? config->data_bitrate : MCP2518FD_DEFAULT_DATA_BITRATE) : 0;
    mcp2518fd_bit_timing_t timing;
    if (!mcp2518fd_compute_bit_timing(clock, bitrate, data_bitrate, &timing)) return MCP2518FD_ERR_INVALID_ARG;
    uint32_t requested_spi = config->spi_baudrate_hz ? config->spi_baudrate_hz : spi_get_baudrate(config->spi);
    if (requested_spi == 0 || requested_spi > clock / 40u * 17u) return MCP2518FD_ERR_INVALID_ARG;
    for (size_t i = 0; i < config->filter_count; i++) {
        uint32_t object, mask;
        if (!mcp2518fd_filter_pack(&config->filters[i], &object, &mask)) return MCP2518FD_ERR_INVALID_ARG;
    }

    memset(bus, 0, sizeof(*bus));
    bus->spi = config->spi;
    bus->cs_pin = config->cs_pin;
    bus->int_pin = config->int_pin;
    bus->fd_enabled = config->fd_enabled;
    gpio_init(bus->cs_pin);
    gpio_put(bus->cs_pin, 1);
    gpio_set_dir(bus->cs_pin, GPIO_OUT);
    if (bus->int_pin != MCP2518FD_NO_INT_PIN) {
        gpio_init((uint)bus->int_pin);
        gpio_set_dir((uint)bus->int_pin, GPIO_IN);
        gpio_pull_up((uint)bus->int_pin);
    }
    /* Safe even with a 4 MHz crystal before its PLL is enabled. */
    spi_set_baudrate(bus->spi, requested_spi < 1000000u ? requested_spi : 1000000u);
    sleep_us(3000);
    uint32_t value;
    mcp2518fd_result_t result = MCP2518FD_ERR_NO_DEVICE;
    if (!read_word(bus, REG_OSC, &value) || (value & (1u << 10)) == 0) goto fail;
    result = switch_mode(bus, MODE_CONFIG);
    if (result != MCP2518FD_OK) goto fail;
    const uint8_t reset[] = {0, 0};
    gpio_put(bus->cs_pin, 0);
    spi_write_blocking(bus->spi, reset, sizeof(reset));
    gpio_put(bus->cs_pin, 1);
    sleep_us(3000);
    result = wait_bits(bus, REG_CON, 7u << 21, MODE_CONFIG << 21);
    if (result != MCP2518FD_OK) goto fail;
    write_byte(bus, REG_OSC, config->oscillator_hz == 4000000u ? 1u : 0u);
    uint32_t ready = (1u << 10) | (config->oscillator_hz == 4000000u ? 1u << 8 : 0);
    result = wait_bits(bus, REG_OSC, ready | (1u << 12), ready);
    if (result != MCP2518FD_OK) goto fail;
    if (spi_set_baudrate(bus->spi, requested_spi) > clock / 40u * 17u) {
        result = MCP2518FD_ERR_INVALID_ARG;
        goto fail;
    }
    write_word(bus, REG_CON, CON_CONFIG);
    write_word(bus, REG_NBTCFG, timing.nbtcfg);
    write_word(bus, REG_DBTCFG, timing.dbtcfg);
    write_word(bus, REG_TDC, timing.tdc);
    /* Detect an absent/miswired device and failed configuration writes. */
    result = MCP2518FD_ERR_SPI;
    if (!read_word(bus, REG_NBTCFG, &value) || value != timing.nbtcfg ||
        !read_word(bus, REG_DBTCFG, &value) || value != timing.dbtcfg ||
        !read_word(bus, REG_CON, &value) || (value & 0x001fffffu) != (CON_CONFIG & 0x001fffffu)) goto fail;

    write_byte(bus, REG_ECCCON, 7u); /* ECC enabled, both errors detected */
    const uint8_t zero[64] = {0};
    for (uint16_t offset = 0; offset < RAM_SIZE; offset += sizeof(zero)) {
        write_bytes(bus, RAM_BASE + offset, zero, sizeof(zero));
    }
    write_byte(bus, REG_ECCSTAT, 0);
    write_word(bus, REG_TXCON, (7u << 29) | ((MCP2518FD_TX_DEPTH - 1) << 24) | (1u << 7));
    write_word(bus, REG_RXCON, (7u << 29) | ((MCP2518FD_RX_DEPTH - 1) << 24) | 9u);
    for (unsigned i = 0; i < MCP2518FD_MAX_FILTERS; i++) write_byte(bus, REG_FLTCON + i, 0);
    size_t filters = config->filter_count ? config->filter_count : 1;
    for (size_t i = 0; i < filters; i++) {
        uint32_t object = 0, mask = 0;
        if (config->filter_count != 0) mcp2518fd_filter_pack(&config->filters[i], &object, &mask);
        write_word(bus, (uint16_t)(REG_FLTOBJ + i * 8), object);
        write_word(bus, (uint16_t)(REG_FLTOBJ + i * 8 + 4), mask);
        write_byte(bus, (uint16_t)(REG_FLTCON + i), 0x82u); /* enable, route to FIFO2 */
    }
    /* RX, overflow, CAN/system error and ECC; no permanently asserted TX
       not-full interrupt. No MCU interrupt handler is installed. */
    write_word(bus, REG_INT, (1u << 17) | (1u << 27) | (1u << 29) | (1u << 28) | (1u << 24));
    result = switch_mode(bus, mode_code(bus, config->mode));
    if (result != MCP2518FD_OK) goto fail;
    result = wait_bits(bus, REG_TXCON, FIFO_RESET, 0);
    if (result != MCP2518FD_OK) goto fail;
    result = wait_bits(bus, REG_RXCON, FIFO_RESET, 0);
    if (result != MCP2518FD_OK) goto fail;
    bus->mode = config->mode;
    bus->initialised = true;
    return MCP2518FD_OK;

fail:
    /* Keep cleanup traffic at the startup-safe clock after any failure. */
    spi_set_baudrate(bus->spi, 1000000u);
    (void)switch_mode(bus, MODE_CONFIG);
    spi_set_baudrate(bus->spi, requested_spi);
    release_pins(bus);
    return result;
}

void mcp2518fd_bus_deinit(mcp2518fd_bus_t *bus)
{
    if (bus == NULL || !bus->initialised) return;
    (void)switch_mode(bus, MODE_CONFIG);
    release_pins(bus);
}

mcp2518fd_result_t mcp2518fd_bus_send(mcp2518fd_bus_t *bus, const can_fd_message_t *message)
{
    uint8_t object[MCP2518FD_OBJECT_SIZE];
    if (bus == NULL || !mcp2518fd_frame_pack(message, object)) return MCP2518FD_ERR_INVALID_ARG;
    if (!bus->initialised) return MCP2518FD_ERR_NOT_INITIALISED;
    if (message->fd && !bus->fd_enabled) return MCP2518FD_ERR_INVALID_ARG;
    if (bus->mode == MCP2518FD_MODE_LISTEN_ONLY) return MCP2518FD_ERR_MONITOR_MODE;
    mcp2518fd_result_t result = check_ram(bus);
    if (result != MCP2518FD_OK) return result;
    uint32_t status;
    if (!read_word(bus, REG_TXSTA, &status)) return MCP2518FD_ERR_SPI;
    if ((status & 1u) == 0) return MCP2518FD_ERR_TX_FULL;
    uint16_t address;
    result = fifo_address(bus, REG_TXUA, &address);
    if (result != MCP2518FD_OK) return result;
    write_bytes(bus, address, object, sizeof(object));
    /* Single byte writes avoid a read-modify-write race with TXREQ clearing. */
    write_byte(bus, REG_TXCON + 1, 3u); /* UINC and TXREQ */
    bus->stats.queued++;
    return MCP2518FD_OK;
}

mcp2518fd_result_t mcp2518fd_bus_receive(mcp2518fd_bus_t *bus, can_fd_message_t *message)
{
    if (bus == NULL || message == NULL) return MCP2518FD_ERR_INVALID_ARG;
    if (!bus->initialised) return MCP2518FD_ERR_NOT_INITIALISED;
    mcp2518fd_result_t result = check_ram(bus);
    if (result != MCP2518FD_OK) return result;
    uint32_t status;
    if (!read_word(bus, REG_RXSTA, &status)) return MCP2518FD_ERR_SPI;
    if ((status & 1u) == 0) return MCP2518FD_ERR_RX_EMPTY;
    uint16_t address;
    result = fifo_address(bus, REG_RXUA, &address);
    if (result != MCP2518FD_OK) return result;
    uint8_t object[MCP2518FD_OBJECT_SIZE];
    if (!read_bytes(bus, address, object, sizeof(object))) return MCP2518FD_ERR_SPI;
    result = check_ram(bus); /* ECC may have been raised by the RAM read */
    if (result != MCP2518FD_OK) return result;
    can_fd_message_t received;
    bool valid = mcp2518fd_frame_unpack(object, &received) && (bus->fd_enabled || !received.fd);
    write_byte(bus, REG_RXCON + 1, 1u); /* consume even a malformed object */
    if (!valid) {
        bus->stats.invalid_frames++;
        return MCP2518FD_ERR_INVALID_FRAME;
    }
    *message = received;
    bus->stats.received++;
    return MCP2518FD_OK;
}

bool mcp2518fd_bus_can_send(mcp2518fd_bus_t *bus)
{
    uint32_t status;
    return bus != NULL && bus->initialised && bus->mode != MCP2518FD_MODE_LISTEN_ONLY &&
           check_ram(bus) == MCP2518FD_OK && read_word(bus, REG_TXSTA, &status) && (status & 1u) != 0;
}

bool mcp2518fd_bus_interrupt_pending(mcp2518fd_bus_t *bus)
{
    return bus != NULL && bus->initialised &&
           (bus->int_pin == MCP2518FD_NO_INT_PIN || !gpio_get((uint)bus->int_pin));
}

mcp2518fd_result_t mcp2518fd_bus_set_mode(mcp2518fd_bus_t *bus, mcp2518fd_mode_t mode)
{
    if (bus == NULL || !valid_mode(mode)) return MCP2518FD_ERR_INVALID_ARG;
    if (!bus->initialised) return MCP2518FD_ERR_NOT_INITIALISED;
    mcp2518fd_result_t result = switch_mode(bus, MODE_CONFIG);
    if (result == MCP2518FD_OK) result = switch_mode(bus, mode_code(bus, mode));
    if (result == MCP2518FD_OK) result = wait_bits(bus, REG_TXCON, FIFO_RESET, 0);
    if (result == MCP2518FD_OK) result = wait_bits(bus, REG_RXCON, FIFO_RESET, 0);
    if (result == MCP2518FD_OK) bus->mode = mode;
    else {
        /* Hardware mode is uncertain: callers must reinitialise. */
        (void)switch_mode(bus, MODE_CONFIG);
        release_pins(bus);
    }
    return result;
}

mcp2518fd_result_t mcp2518fd_bus_get_stats(mcp2518fd_bus_t *bus, mcp2518fd_bus_stats_t *stats)
{
    if (bus == NULL || stats == NULL) return MCP2518FD_ERR_INVALID_ARG;
    if (!bus->initialised) return MCP2518FD_ERR_NOT_INITIALISED;
    uint32_t rx, interrupts;
    if (!read_word(bus, REG_RXSTA, &rx) || !read_word(bus, REG_INT, &interrupts) ||
        !read_word(bus, REG_TREC, &bus->stats.trec)) return MCP2518FD_ERR_SPI;
    if (rx & 8u) {
        bus->stats.rx_overflow++;
        write_byte(bus, REG_RXSTA, 0xf7u); /* clear RXOVIF only */
    }
    if (interrupts & ((1u << 13) | (1u << 12))) {
        bus->stats.controller_errors++;
        write_byte(bus, REG_INT + 1, 0xcfu); /* clear CERRIF/SERRIF only */
    }
    mcp2518fd_result_t result = check_ram(bus);
    *stats = bus->stats;
    return result;
}

const char *mcp2518fd_result_name(mcp2518fd_result_t result)
{
    switch (result) {
        case MCP2518FD_OK: return "ok";
        case MCP2518FD_ERR_INVALID_ARG: return "invalid argument";
        case MCP2518FD_ERR_NOT_INITIALISED: return "not initialised";
        case MCP2518FD_ERR_NO_DEVICE: return "no device";
        case MCP2518FD_ERR_SPI: return "SPI CRC failure";
        case MCP2518FD_ERR_TIMEOUT: return "controller timeout";
        case MCP2518FD_ERR_MONITOR_MODE: return "listen-only mode";
        case MCP2518FD_ERR_TX_FULL: return "TX FIFO full";
        case MCP2518FD_ERR_RX_EMPTY: return "RX FIFO empty";
        case MCP2518FD_ERR_INVALID_FRAME: return "invalid frame";
        case MCP2518FD_ERR_RAM: return "RAM fault: reinitialise";
        default: return "unknown";
    }
}
