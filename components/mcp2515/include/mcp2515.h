/*
 * mcp2515 - CAN 2.0B over an MCP2515 (or pin/register-compatible clone, such
 * as the XL2515 on Waveshare's RP2350-CAN) SPI CAN controller.
 *
 * Unlike components/can (can2040 on a PIO block, which owns hardware and
 * gets its frames from an interrupt), this is a device on a shared SPI bus:
 * the application configures the SPI peripheral and its SCK/MOSI/MISO pins,
 * same as i2c_device, and this component drives chip select and the
 * register protocol over it. There is no interrupt-context receive path —
 * mcp2515_bus_receive() polls the controller's own status register over
 * SPI, so it must be called often enough that the controller's two-message
 * hardware RX depth does not overflow. `int_pin`, when given, only lets the
 * caller cheaply skip that poll while the bus is idle; see
 * mcp2515_bus_interrupt_pending().
 */

#ifndef PICO_FRAMEWORK_MCP2515_H
#define PICO_FRAMEWORK_MCP2515_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/spi.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP2515_DEFAULT_BITRATE 500000u
#define MCP2515_MAX_BITRATE 1000000u

/* Skip owning an interrupt pin; mcp2515_bus_interrupt_pending() always
   reports true and the caller polls unconditionally. */
#define MCP2515_NO_INT_PIN (-1)

/* RXF0..RXF2 share RXM0, RXF3..RXF5 share RXM1 — see mcp2515_bus_config_t. */
#define MCP2515_MAX_FILTERS 6u

typedef enum {
    MCP2515_OK = 0,
    MCP2515_ERR_INVALID_ARG,
    MCP2515_ERR_NO_DEVICE,     /* nothing answered the SPI reset/readback */
    MCP2515_ERR_NOT_INITIALISED,
    MCP2515_ERR_MODE_TIMEOUT,  /* controller never reported the requested mode */
    MCP2515_ERR_MONITOR_MODE,  /* send() while in MCP2515_MODE_LISTEN_ONLY */
    MCP2515_ERR_TX_FULL,
    MCP2515_ERR_RX_EMPTY,
} mcp2515_result_t;

typedef enum {
    MCP2515_MODE_NORMAL = 0,
    MCP2515_MODE_LOOPBACK,     /* self-test: transmits loop back to the receiver, nothing goes on the wire */
    MCP2515_MODE_LISTEN_ONLY,  /* receive-only; cannot transmit, does not ACK */
} mcp2515_mode_t;

typedef struct {
    /*
     * An already-initialised bus: application calls spi_init() and
     * gpio_set_function() on SCK/MOSI/MISO before mcp2515_bus_init(), the
     * same division of ownership as i2c_device. Several devices may share
     * one spi_inst_t; this component only ever touches `cs_pin`.
     */
    spi_inst_t *spi;

    /* 0 leaves the SPI baud rate exactly as the caller configured it.
       Otherwise mcp2515_bus_init() calls spi_set_baudrate() once, at
       init — a bus shared with a device needing a different rate must be
       re-set by the caller before talking to that other device. */
    uint32_t spi_baudrate_hz;

    /* Software-driven chip select; the Pico SPI peripheral has no hardware
       CS. This component owns the pin and drives it itself. */
    uint cs_pin;

    /* Active-low INT output, or MCP2515_NO_INT_PIN. Optional: see the file
       comment above and mcp2515_bus_interrupt_pending(). */
    int int_pin;

    /* The board's CAN crystal/resonator frequency — a fixed physical fact,
       not a guess; wrong here silently mistimes the bus. No default. */
    uint32_t oscillator_hz;

    /* 0 selects MCP2515_DEFAULT_BITRATE. mcp2515_compute_bit_timing() must
       reach it exactly from oscillator_hz or mcp2515_bus_init() fails. */
    uint32_t bitrate;

    mcp2515_mode_t mode;

    /*
     * Optional hardware acceptance filters. NULL/0 accepts every valid
     * frame, matching components/can's software-filter convention — but
     * unlike that component's arbitrary filter list, this is real
     * controller hardware with two independent 3-filter banks: filters
     * [0..2] share one mask (RXM0) and [3..5] share a second (RXM1), so
     * every filter within each half of the array must use the identical
     * `.mask`. mcp2515_bus_init() validates this and fails otherwise.
     *
     * Every filter's mask must include CAN_FLAG_EXTENDED (the controller
     * matches a filter's standard/extended flag exactly, never masks it)
     * and must not include CAN_FLAG_RTR (the controller has no hardware
     * remote-request filter).
     */
    const can_filter_t *filters;
    size_t filter_count;
} mcp2515_bus_config_t;

typedef struct {
    uint32_t received;          /* frames returned by mcp2515_bus_receive() */
    uint32_t transmitted;       /* frames the controller confirmed sent (TXnIF) */
    uint32_t rx_overflow;       /* EFLG RXnOVR: a full hardware RX buffer dropped a frame */
    uint32_t controller_errors; /* CANINTF.ERRIF observed; see last_eflg for detail */
    uint8_t last_eflg;          /* EFLG snapshot at the last controller_errors increment */
} mcp2515_bus_stats_t;

/*
 * Public so instances can live in static storage; fields are private to the
 * component.
 */
typedef struct {
    spi_inst_t *spi;
    uint cs_pin;
    int int_pin;
    mcp2515_mode_t mode;
    uint8_t next_tx_buffer;
    mcp2515_bus_stats_t stats;
    bool initialised;
} mcp2515_bus_t;

/*
 * Reset the controller, program its bit timing and filters, and bring it up
 * in `config->mode`. Not reentrant and not interrupt-safe; call it and
 * mcp2515_bus_deinit() only from a context that is not concurrently calling
 * any other function on this bus.
 */
mcp2515_result_t mcp2515_bus_init(mcp2515_bus_t *bus, const mcp2515_bus_config_t *config);

/* Return the chip to configuration mode and release cs_pin/int_pin. Does
   not touch the shared spi_inst_t. */
void mcp2515_bus_deinit(mcp2515_bus_t *bus);

/* Load and request transmission of one frame in the next free TX buffer (of
   three). Returns MCP2515_ERR_TX_FULL rather than blocking. */
mcp2515_result_t mcp2515_bus_send(mcp2515_bus_t *bus, const can_message_t *message);

/* Whether mcp2515_bus_send() can currently accept a frame. */
bool mcp2515_bus_can_send(mcp2515_bus_t *bus);

/* Pop one accepted frame out of the controller's RX buffers over SPI.
   MCP2515_ERR_RX_EMPTY when neither buffer holds one. */
mcp2515_result_t mcp2515_bus_receive(mcp2515_bus_t *bus, can_message_t *message);

/* Frames currently held in hardware: 0, 1, or 2. */
size_t mcp2515_bus_available(mcp2515_bus_t *bus);

/*
 * Cheap pre-check before a receive poll: true when int_pin is unset (so the
 * caller has no cheaper option than to just poll) or is currently asserted
 * (active low). False means "definitely nothing pending" without an SPI
 * transaction.
 */
bool mcp2515_bus_interrupt_pending(mcp2515_bus_t *bus);

/* Switch operating mode, e.g. into MCP2515_MODE_LOOPBACK for a wiring
   self-test with no second CAN node. Blocks briefly for the controller to
   confirm the switch; MCP2515_ERR_MODE_TIMEOUT if it never does. */
mcp2515_result_t mcp2515_bus_set_mode(mcp2515_bus_t *bus, mcp2515_mode_t mode);

/* Refresh and return the counters. Draining TXnIF/EFLG needs an SPI
   transaction, so call this periodically even if not otherwise polling. */
mcp2515_result_t mcp2515_bus_get_stats(mcp2515_bus_t *bus, mcp2515_bus_stats_t *stats);

void mcp2515_bus_clear_stats(mcp2515_bus_t *bus);

const char *mcp2515_result_name(mcp2515_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MCP2515_H */
