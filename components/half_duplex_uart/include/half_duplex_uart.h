/*
 * half_duplex_uart - 8N1 UART over a single shared wire, via PIO.
 *
 * Intended for daisy-chained smart servos — Dynamixel AX-12, Feetech STS — and
 * any other bus where one line carries both directions and the host speaks
 * first. Two PIO state machines share the data pin: the transmitter drives it
 * only while a byte is going out and releases it afterwards, so the receiver
 * can listen on the same pad.
 *
 * The API is synchronous, per DESIGN_DOC.md section 8. Its contracts:
 *
 *   Timeouts       Every read takes an explicit timeout. A transfer that gets
 *                  no reply returns HALF_DUPLEX_UART_ERR_TIMEOUT, never blocks
 *                  forever.
 *   Errors         Reported by return value. A short or absent reply is a
 *                  timeout; a framing error drops the byte in the PIO program
 *                  and therefore also surfaces as a short reply.
 *   Buffers        Caller-owned, always. The component allocates nothing and
 *                  keeps no pointer to a caller's buffer past the call.
 *   Peripheral     The component claims two state machines on the PIO block it
 *                  is given, and owns them until deinit. The data and direction
 *                  pins belong to it while initialised.
 *
 * Not interrupt-driven and not thread-safe: one bus is driven from one context.
 */

#ifndef PICO_FRAMEWORK_HALF_DUPLEX_UART_H
#define PICO_FRAMEWORK_HALF_DUPLEX_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/pio.h"

#include "half_duplex_uart_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pass as `direction_pin` when the board has no transceiver to steer. */
#define HALF_DUPLEX_UART_NO_DIRECTION_PIN (-1)

typedef enum {
    HALF_DUPLEX_UART_OK = 0,
    HALF_DUPLEX_UART_ERR_INVALID_ARG,
    HALF_DUPLEX_UART_ERR_BAUDRATE,        /* unreachable or out of tolerance */
    HALF_DUPLEX_UART_ERR_NO_STATE_MACHINE,
    HALF_DUPLEX_UART_ERR_NO_PROGRAM_SPACE,
    HALF_DUPLEX_UART_ERR_TIMEOUT,         /* no reply, or it stopped early */
    HALF_DUPLEX_UART_ERR_OVERFLOW,        /* reply longer than the buffer */
} half_duplex_uart_result_t;

typedef struct {
    /* PIO block to claim two state machines on. */
    PIO pio;

    /* The shared data line: transmit and receive both use this pin. */
    uint pin;

    /*
     * Optional direction signal for a transceiver or level shifter. Driven
     * high while transmitting and low while receiving. Set to
     * HALF_DUPLEX_UART_NO_DIRECTION_PIN when the line is driven directly.
     */
    int direction_pin;

    uint32_t baudrate;

    /*
     * True when the receiver hears the bytes this driver transmits, which is
     * the case whenever transmit and receive share a pad with nothing to mute
     * the echo — the common wiring for a servo bus. The driver then discards
     * exactly as many bytes as it sent before looking for a reply.
     *
     * Set false only with a transceiver that disables its receiver while
     * driving; leaving it true there would eat the first bytes of every reply.
     */
    bool receives_own_transmission;
} half_duplex_uart_config_t;

typedef struct {
    PIO pio;
    uint sm_tx;
    uint sm_rx;
    uint offset_tx;
    uint offset_rx;
    uint pin;
    int direction_pin;
    uint32_t baudrate;
    bool receives_own_transmission;
    half_duplex_uart_timing_t timing;
    bool initialised;
} half_duplex_uart_t;

/*
 * Claim two state machines, load both programs, and configure the pins.
 *
 * Fails with HALF_DUPLEX_UART_ERR_BAUDRATE when the rate cannot be reached
 * within HALF_DUPLEX_UART_MAX_ERROR_PERMILLE of what was asked. That is a hard
 * error on purpose: a bus clocked a few percent wrong works until it doesn't.
 */
half_duplex_uart_result_t half_duplex_uart_init(half_duplex_uart_t *bus,
                                                const half_duplex_uart_config_t *config);

/* Release both state machines and both programs. Leaves the pins as they are. */
void half_duplex_uart_deinit(half_duplex_uart_t *bus);

/*
 * Change the bus rate. Same tolerance rule as init. Anything already in the
 * FIFOs is discarded, so do not call this mid-transaction.
 */
half_duplex_uart_result_t half_duplex_uart_set_baudrate(half_duplex_uart_t *bus,
                                                        uint32_t baudrate);

/* What the bus is actually clocked at, after 8.8 quantisation. */
const half_duplex_uart_timing_t *half_duplex_uart_get_timing(const half_duplex_uart_t *bus);

/*
 * Send `len` bytes and return once the last stop bit has left the state
 * machine and the line has been released.
 *
 * When receives_own_transmission is set, the echo of these bytes is discarded
 * before returning, so a following read sees only the reply.
 */
half_duplex_uart_result_t half_duplex_uart_write(half_duplex_uart_t *bus,
                                                 const uint8_t *data, size_t len);

/*
 * Read up to `capacity` bytes into `buffer`, storing the count in `received`.
 *
 * Returns when `capacity` bytes have arrived, or when the line has been quiet
 * for `inter_byte_timeout_us` after at least one byte, or with
 * HALF_DUPLEX_UART_ERR_TIMEOUT when nothing arrives within `timeout_us`.
 * `received` is always written, so a timeout still reports a partial reply.
 */
half_duplex_uart_result_t half_duplex_uart_read(half_duplex_uart_t *bus,
                                                uint8_t *buffer, size_t capacity,
                                                size_t *received,
                                                uint32_t timeout_us,
                                                uint32_t inter_byte_timeout_us);

/*
 * Read exactly `len` bytes, or fail. The common case for a protocol whose
 * reply length is known from its header.
 */
half_duplex_uart_result_t half_duplex_uart_read_exact(half_duplex_uart_t *bus,
                                                      uint8_t *buffer, size_t len,
                                                      uint32_t timeout_us);

/*
 * write() then read_exact(): the shape of nearly every servo transaction.
 * `timeout_us` covers the reply only; the time to transmit is not counted
 * against it.
 */
half_duplex_uart_result_t half_duplex_uart_transfer(half_duplex_uart_t *bus,
                                                    const uint8_t *tx, size_t tx_len,
                                                    uint8_t *rx, size_t rx_len,
                                                    uint32_t timeout_us);

/* Drop anything already received. Useful before starting a transaction on a
   bus that may have been left mid-reply. */
void half_duplex_uart_flush_rx(half_duplex_uart_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_HALF_DUPLEX_UART_H */
