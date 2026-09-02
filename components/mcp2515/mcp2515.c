#include <string.h>

#include "hardware/gpio.h"
#include "pico/time.h"

#include "mcp2515.h"
#include "mcp2515_filters.h"
#include "mcp2515_frame.h"
#include "mcp2515_timing.h"

/* SPI commands (MCP2515 datasheet section 12). */
#define CMD_RESET 0xC0u
#define CMD_READ 0x03u
#define CMD_WRITE 0x02u
#define CMD_BITMOD 0x05u
#define CMD_READ_STATUS 0xA0u
#define CMD_LOAD_TX_BASE 0x40u /* | (txb * 2) selects TXBnSIDH */
#define CMD_RTS_BASE 0x80u     /* | (1 << txb) */
#define CMD_READ_RX_BASE 0x90u /* | (rxb * 4) selects RXBnSIDH */

/* Registers touched by this driver. */
#define REG_CANSTAT 0x0Eu
#define REG_CANCTRL 0x0Fu
#define REG_RXF0SIDH 0x00u
#define REG_RXF1SIDH 0x04u
#define REG_RXF2SIDH 0x08u
#define REG_RXF3SIDH 0x10u
#define REG_RXF4SIDH 0x14u
#define REG_RXF5SIDH 0x18u
#define REG_RXM0SIDH 0x20u
#define REG_RXM1SIDH 0x24u
#define REG_CNF3 0x28u
#define REG_CNF2 0x29u
#define REG_CNF1 0x2Au
#define REG_CANINTE 0x2Bu
#define REG_CANINTF 0x2Cu
#define REG_EFLG 0x2Du
#define REG_RXB0CTRL 0x60u
#define REG_RXB1CTRL 0x70u

/* CANSTAT/CANCTRL REQOP/OPMOD field (bits 7:5). */
#define REQOP_NORMAL 0x00u
#define REQOP_LOOPBACK 0x02u
#define REQOP_LISTEN_ONLY 0x03u
#define REQOP_CONFIG 0x04u

/* CANINTF/CANINTE bits. */
#define CANINTF_RX0IF (1u << 0)
#define CANINTF_RX1IF (1u << 1)
#define CANINTF_TX0IF (1u << 2)
#define CANINTF_TX1IF (1u << 3)
#define CANINTF_TX2IF (1u << 4)
#define CANINTF_ERRIF (1u << 5)

/* EFLG bits. */
#define EFLG_RX0OVR (1u << 6)
#define EFLG_RX1OVR (1u << 7)

/* READ STATUS response bits. */
#define STATUS_RX0IF (1u << 0)
#define STATUS_RX1IF (1u << 1)
#define STATUS_TXREQ0 (1u << 2)
#define STATUS_TXREQ1 (1u << 4)
#define STATUS_TXREQ2 (1u << 6)

static const uint8_t TXREQ_BIT[3] = {STATUS_TXREQ0, STATUS_TXREQ1, STATUS_TXREQ2};
static const uint8_t FILTER_REG[MCP2515_FILTER_SLOTS] = {
    REG_RXF0SIDH, REG_RXF1SIDH, REG_RXF2SIDH, REG_RXF3SIDH, REG_RXF4SIDH, REG_RXF5SIDH,
};

/* -----------------------------------------------------------------------
 * SPI transport
 * -------------------------------------------------------------------- */

static inline void cs_select(mcp2515_bus_t *bus)
{
    gpio_put(bus->cs_pin, 0);
}

static inline void cs_deselect(mcp2515_bus_t *bus)
{
    gpio_put(bus->cs_pin, 1);
}

static void mcp2515_reset(mcp2515_bus_t *bus, uint32_t oscillator_hz)
{
    const uint8_t cmd = CMD_RESET;
    cs_select(bus);
    spi_write_blocking(bus->spi, &cmd, 1);
    cs_deselect(bus);

    /* Measured in the controller's clock rather than ours, so it depends on
       the board's crystal; see mcp2515_reset_delay_us(). */
    sleep_us(mcp2515_reset_delay_us(oscillator_hz));
}

static void write_register(mcp2515_bus_t *bus, uint8_t reg, uint8_t value)
{
    const uint8_t tx[3] = {CMD_WRITE, reg, value};
    cs_select(bus);
    spi_write_blocking(bus->spi, tx, sizeof(tx));
    cs_deselect(bus);
}

static void write_registers(mcp2515_bus_t *bus, uint8_t reg, const uint8_t *data, size_t len)
{
    const uint8_t header[2] = {CMD_WRITE, reg};
    cs_select(bus);
    spi_write_blocking(bus->spi, header, sizeof(header));
    spi_write_blocking(bus->spi, data, len);
    cs_deselect(bus);
}

static uint8_t read_register(mcp2515_bus_t *bus, uint8_t reg)
{
    const uint8_t tx[2] = {CMD_READ, reg};
    uint8_t value = 0;
    cs_select(bus);
    spi_write_blocking(bus->spi, tx, sizeof(tx));
    spi_read_blocking(bus->spi, 0xFF, &value, 1);
    cs_deselect(bus);
    return value;
}

static void bit_modify(mcp2515_bus_t *bus, uint8_t reg, uint8_t mask, uint8_t value)
{
    const uint8_t tx[4] = {CMD_BITMOD, reg, mask, value};
    cs_select(bus);
    spi_write_blocking(bus->spi, tx, sizeof(tx));
    cs_deselect(bus);
}

static uint8_t read_status(mcp2515_bus_t *bus)
{
    const uint8_t cmd = CMD_READ_STATUS;
    uint8_t value = 0;
    cs_select(bus);
    spi_write_blocking(bus->spi, &cmd, 1);
    spi_read_blocking(bus->spi, 0xFF, &value, 1);
    cs_deselect(bus);
    return value;
}

/* -----------------------------------------------------------------------
 * Mode switching
 * -------------------------------------------------------------------- */

static uint8_t reqop_for(mcp2515_mode_t mode)
{
    switch (mode) {
        case MCP2515_MODE_LOOPBACK:    return REQOP_LOOPBACK;
        case MCP2515_MODE_LISTEN_ONLY: return REQOP_LISTEN_ONLY;
        case MCP2515_MODE_NORMAL:
        default:                       return REQOP_NORMAL;
    }
}

static mcp2515_result_t switch_mode(mcp2515_bus_t *bus, uint8_t reqop)
{
    bit_modify(bus, REG_CANCTRL, 0xE0u, (uint8_t)(reqop << 5));
    for (int attempt = 0; attempt < 50; attempt++) {
        if ((read_register(bus, REG_CANSTAT) >> 5) == reqop) {
            return MCP2515_OK;
        }
        sleep_us(20);
    }
    return MCP2515_ERR_MODE_TIMEOUT;
}

/* -----------------------------------------------------------------------
 * Filters and masks
 * -------------------------------------------------------------------- */

static void write_filter(mcp2515_bus_t *bus, uint8_t sidh_reg, uint32_t packed_id)
{
    uint8_t bytes[4];
    mcp2515_frame_pack_id(packed_id, bytes);
    write_registers(bus, sidh_reg, bytes, sizeof(bytes));
}

/*
 * Both receive buffers are always live, so a filter set is only ever applied
 * in full: either neither buffer filters, or both do. mcp2515_filter_plan()
 * owns that decision and the 2/4 split between the banks; this writes the
 * result out.
 */
static void configure_filters(mcp2515_bus_t *bus, const mcp2515_filter_plan_t *plan)
{
    if (plan->accept_all) {
        write_register(bus, REG_RXB0CTRL, 0x64u); /* RXM=11 (accept all), BUKT=1 */
        write_register(bus, REG_RXB1CTRL, 0x60u); /* RXM=11 (accept all) */
        return;
    }

    for (size_t i = 0; i < MCP2515_FILTER_SLOTS; i++) {
        write_filter(bus, FILTER_REG[i], plan->filter_id[i]);
    }
    write_filter(bus, REG_RXM0SIDH, plan->mask[0]);
    write_filter(bus, REG_RXM1SIDH, plan->mask[1]);

    write_register(bus, REG_RXB0CTRL, 0x04u); /* RXM=00 (use filters), BUKT=1 */
    write_register(bus, REG_RXB1CTRL, 0x00u); /* RXM=00 (use filters) */
}

/* -----------------------------------------------------------------------
 * Stats
 * -------------------------------------------------------------------- */

static void update_stats(mcp2515_bus_t *bus)
{
    const uint8_t canintf = read_register(bus, REG_CANINTF);
    uint8_t clear_mask = 0;

    if (canintf & CANINTF_TX0IF) {
        bus->stats.transmitted++;
        clear_mask |= CANINTF_TX0IF;
    }
    if (canintf & CANINTF_TX1IF) {
        bus->stats.transmitted++;
        clear_mask |= CANINTF_TX1IF;
    }
    if (canintf & CANINTF_TX2IF) {
        bus->stats.transmitted++;
        clear_mask |= CANINTF_TX2IF;
    }

    if (canintf & CANINTF_ERRIF) {
        const uint8_t eflg = read_register(bus, REG_EFLG);
        bus->stats.controller_errors++;
        bus->stats.last_eflg = eflg;
        if (eflg & (EFLG_RX0OVR | EFLG_RX1OVR)) {
            bus->stats.rx_overflow++;
        }
        bit_modify(bus, REG_EFLG, EFLG_RX0OVR | EFLG_RX1OVR, 0);
        clear_mask |= CANINTF_ERRIF;
    }

    if (clear_mask != 0) {
        bit_modify(bus, REG_CANINTF, clear_mask, 0);
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------- */

mcp2515_result_t mcp2515_bus_init(mcp2515_bus_t *bus, const mcp2515_bus_config_t *config)
{
    mcp2515_filter_plan_t plan;
    if (bus == NULL || config == NULL || config->spi == NULL || config->oscillator_hz == 0 ||
        !mcp2515_filter_plan(config->filters, config->filter_count, &plan)) {
        return MCP2515_ERR_INVALID_ARG;
    }

    const uint32_t bitrate = config->bitrate != 0 ? config->bitrate : MCP2515_DEFAULT_BITRATE;
    if (bitrate > MCP2515_MAX_BITRATE) {
        return MCP2515_ERR_INVALID_ARG;
    }
    mcp2515_bit_timing_t timing;
    if (!mcp2515_compute_bit_timing(config->oscillator_hz, bitrate, &timing)) {
        return MCP2515_ERR_INVALID_ARG;
    }

    memset(bus, 0, sizeof(*bus));
    bus->spi = config->spi;
    bus->cs_pin = config->cs_pin;
    bus->int_pin = config->int_pin;

    gpio_init(bus->cs_pin);
    gpio_set_dir(bus->cs_pin, GPIO_OUT);
    gpio_put(bus->cs_pin, 1);

    if (bus->int_pin != MCP2515_NO_INT_PIN) {
        gpio_init((uint)bus->int_pin);
        gpio_set_dir((uint)bus->int_pin, GPIO_IN);
        gpio_pull_up((uint)bus->int_pin);
    }

    if (config->spi_baudrate_hz != 0) {
        spi_set_baudrate(bus->spi, config->spi_baudrate_hz);
    }

    mcp2515_reset(bus, config->oscillator_hz);

    /* A chip that never answers reads back whatever MISO floats to, never
       config mode's 0b100 — the cheapest available "is anything wired up
       and alive at all" check. */
    if ((read_register(bus, REG_CANSTAT) >> 5) != REQOP_CONFIG) {
        return MCP2515_ERR_NO_DEVICE;
    }

    write_register(bus, REG_CNF1, timing.cnf1);
    write_register(bus, REG_CNF2, timing.cnf2);
    write_register(bus, REG_CNF3, timing.cnf3);

    write_register(bus, REG_CANINTE,
                    CANINTF_RX0IF | CANINTF_RX1IF | CANINTF_TX0IF | CANINTF_TX1IF |
                        CANINTF_TX2IF | CANINTF_ERRIF);
    write_register(bus, REG_CANINTF, 0);

    configure_filters(bus, &plan);

    const mcp2515_result_t mode_result = switch_mode(bus, reqop_for(config->mode));
    if (mode_result != MCP2515_OK) {
        return mode_result;
    }

    bus->mode = config->mode;
    bus->initialised = true;
    return MCP2515_OK;
}

void mcp2515_bus_deinit(mcp2515_bus_t *bus)
{
    if (bus == NULL || !bus->initialised) {
        return;
    }

    switch_mode(bus, REQOP_CONFIG); /* best effort: leave the bus quiet */

    gpio_deinit(bus->cs_pin);
    if (bus->int_pin != MCP2515_NO_INT_PIN) {
        gpio_deinit((uint)bus->int_pin);
    }
    bus->initialised = false;
}

mcp2515_result_t mcp2515_bus_send(mcp2515_bus_t *bus, const can_message_t *message)
{
    if (bus == NULL || !can_message_is_valid(message)) {
        return MCP2515_ERR_INVALID_ARG;
    }
    if (!bus->initialised) {
        return MCP2515_ERR_NOT_INITIALISED;
    }
    if (bus->mode == MCP2515_MODE_LISTEN_ONLY) {
        return MCP2515_ERR_MONITOR_MODE;
    }

    const uint8_t status = read_status(bus);
    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        const int idx = (bus->next_tx_buffer + i) % 3;
        if ((status & TXREQ_BIT[idx]) == 0) {
            chosen = idx;
            break;
        }
    }
    if (chosen < 0) {
        return MCP2515_ERR_TX_FULL;
    }

    uint8_t header[MCP2515_FRAME_HEADER_SIZE];
    mcp2515_frame_pack_header(message, header);

    const uint8_t load_cmd = (uint8_t)(CMD_LOAD_TX_BASE | ((uint32_t)chosen * 2u));
    cs_select(bus);
    spi_write_blocking(bus->spi, &load_cmd, 1);
    spi_write_blocking(bus->spi, header, sizeof(header));
    if (!message->remote && message->length > 0) {
        spi_write_blocking(bus->spi, message->data, message->length);
    }
    cs_deselect(bus);

    const uint8_t rts_cmd = (uint8_t)(CMD_RTS_BASE | (1u << chosen));
    cs_select(bus);
    spi_write_blocking(bus->spi, &rts_cmd, 1);
    cs_deselect(bus);

    bus->next_tx_buffer = (uint8_t)((chosen + 1) % 3);
    return MCP2515_OK;
}

bool mcp2515_bus_can_send(mcp2515_bus_t *bus)
{
    if (bus == NULL || !bus->initialised || bus->mode == MCP2515_MODE_LISTEN_ONLY) {
        return false;
    }
    const uint8_t status = read_status(bus);
    const uint8_t all_busy = TXREQ_BIT[0] | TXREQ_BIT[1] | TXREQ_BIT[2];
    return (status & all_busy) != all_busy;
}

mcp2515_result_t mcp2515_bus_receive(mcp2515_bus_t *bus, can_message_t *message)
{
    if (bus == NULL || message == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }
    if (!bus->initialised) {
        return MCP2515_ERR_NOT_INITIALISED;
    }

    update_stats(bus);

    const uint8_t status = read_status(bus);
    int rxb = -1;
    if (status & STATUS_RX0IF) {
        rxb = 0;
    } else if (status & STATUS_RX1IF) {
        rxb = 1;
    }
    if (rxb < 0) {
        return MCP2515_ERR_RX_EMPTY;
    }

    uint8_t header[MCP2515_FRAME_HEADER_SIZE];
    const uint8_t read_cmd = (uint8_t)(CMD_READ_RX_BASE | ((uint32_t)rxb * 4u));
    cs_select(bus);
    spi_write_blocking(bus->spi, &read_cmd, 1);
    spi_read_blocking(bus->spi, 0xFF, header, sizeof(header));
    mcp2515_frame_unpack_header(header, message);
    if (!message->remote && message->length > 0) {
        spi_read_blocking(bus->spi, 0xFF, message->data, message->length);
    }
    cs_deselect(bus);

    /* The fast read is documented to clear RXnIF itself; bit-modify it
       again anyway; a second clear of an already-clear bit costs one SPI
       transaction and removes any dependence on that behaviour. */
    bit_modify(bus, REG_CANINTF, rxb == 0 ? CANINTF_RX0IF : CANINTF_RX1IF, 0);

    bus->stats.received++;
    return MCP2515_OK;
}

size_t mcp2515_bus_available(mcp2515_bus_t *bus)
{
    if (bus == NULL || !bus->initialised) {
        return 0;
    }
    const uint8_t status = read_status(bus);
    size_t count = 0;
    if (status & STATUS_RX0IF) {
        count++;
    }
    if (status & STATUS_RX1IF) {
        count++;
    }
    return count;
}

bool mcp2515_bus_interrupt_pending(mcp2515_bus_t *bus)
{
    if (bus == NULL || !bus->initialised) {
        return false;
    }
    if (bus->int_pin == MCP2515_NO_INT_PIN) {
        return true;
    }
    return gpio_get((uint)bus->int_pin) == 0;
}

mcp2515_result_t mcp2515_bus_set_mode(mcp2515_bus_t *bus, mcp2515_mode_t mode)
{
    if (bus == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }
    if (!bus->initialised) {
        return MCP2515_ERR_NOT_INITIALISED;
    }
    const mcp2515_result_t result = switch_mode(bus, reqop_for(mode));
    if (result == MCP2515_OK) {
        bus->mode = mode;
    }
    return result;
}

mcp2515_result_t mcp2515_bus_get_stats(mcp2515_bus_t *bus, mcp2515_bus_stats_t *stats)
{
    if (bus == NULL || stats == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }
    if (!bus->initialised) {
        return MCP2515_ERR_NOT_INITIALISED;
    }
    update_stats(bus);
    *stats = bus->stats;
    return MCP2515_OK;
}

void mcp2515_bus_clear_stats(mcp2515_bus_t *bus)
{
    if (bus != NULL) {
        memset(&bus->stats, 0, sizeof(bus->stats));
    }
}

const char *mcp2515_result_name(mcp2515_result_t result)
{
    switch (result) {
        case MCP2515_OK:                  return "ok";
        case MCP2515_ERR_INVALID_ARG:     return "invalid argument";
        case MCP2515_ERR_NO_DEVICE:       return "no device";
        case MCP2515_ERR_NOT_INITIALISED: return "not initialised";
        case MCP2515_ERR_MODE_TIMEOUT:    return "mode switch timed out";
        case MCP2515_ERR_MONITOR_MODE:    return "monitor mode";
        case MCP2515_ERR_TX_FULL:         return "transmit buffers full";
        case MCP2515_ERR_RX_EMPTY:        return "receive buffers empty";
        default:                          return "unknown";
    }
}
