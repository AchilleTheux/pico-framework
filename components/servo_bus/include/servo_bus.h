/*
 * servo_bus - synchronous Protocol 1.0 transactions over a half-duplex UART.
 *
 * Joins servo_protocol.h (what a packet looks like) to half_duplex_uart.h (how
 * bytes get onto one shared wire), and adds the two things every caller
 * otherwise reinvents: retrying a transaction that was corrupted or lost, and
 * counting how often that happens.
 *
 * The `ax12` and `feetech` components are register maps on top of this. They
 * differ in their registers and, for some Feetech models, in byte order —
 * nothing else.
 *
 * Contracts, following DESIGN_DOC.md section 8:
 *
 *   Timeouts   Every transaction is bounded. The wait is the time to transmit
 *              the reply at the current baud rate plus `response_timeout_us`,
 *              so raising the bus speed does not silently tighten the margin.
 *   Errors     By return value. A servo that answers but reports a fault of
 *              its own is a successful transaction: the fault is handed back
 *              through `error_out`, because a voltage warning is not a reason
 *              to discard a valid reading.
 *   Buffers    Caller-owned. Packets are built on the stack and nothing is
 *              retained after the call.
 *   Peripheral The bus borrows a half_duplex_uart_t the caller owns and
 *              initialised; it does not configure or release it.
 *
 * Synchronous and not thread-safe: one bus, one calling context.
 */

#ifndef PICO_FRAMEWORK_SERVO_BUS_H
#define PICO_FRAMEWORK_SERVO_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "half_duplex_uart.h"
#include "servo_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Time allowed for a servo to begin answering, on top of transmission time.
   An AX-12 leaves its factory return delay at 500 us. */
#ifndef SERVO_BUS_DEFAULT_RESPONSE_TIMEOUT_US
#define SERVO_BUS_DEFAULT_RESPONSE_TIMEOUT_US 3000u
#endif

#ifndef SERVO_BUS_DEFAULT_MAX_RETRIES
#define SERVO_BUS_DEFAULT_MAX_RETRIES 2u
#endif

typedef enum {
    SERVO_BUS_OK = 0,
    SERVO_BUS_ERR_INVALID_ARG,
    SERVO_BUS_ERR_TRANSPORT,     /* the UART itself refused the transfer */
    SERVO_BUS_ERR_TIMEOUT,       /* no reply, or it stopped short */
    SERVO_BUS_ERR_CHECKSUM,      /* a reply arrived corrupted */
    SERVO_BUS_ERR_MALFORMED,     /* framing was not a status packet at all */
    SERVO_BUS_ERR_WRONG_ID,      /* someone else answered */
    SERVO_BUS_ERR_SHORT_REPLY,   /* fewer bytes than the read asked for */
} servo_bus_result_t;

/*
 * How the bus has been behaving. Worth watching: a servo link usually degrades
 * before it fails, and a retry count climbing under load points at power or
 * termination rather than at the code.
 */
typedef struct {
    uint32_t transactions;    /* attempted, counting each retry once */
    uint32_t retries;
    uint32_t timeouts;
    uint32_t checksum_errors;
    uint32_t malformed;
    uint32_t wrong_id;
} servo_bus_stats_t;

typedef struct {
    /* Already initialised by the caller, and outliving this bus. */
    half_duplex_uart_t *uart;

    /* Byte order for multi-byte registers. AX-12 and Feetech STS/SMS are
       little-endian; Feetech SCS is big-endian. */
    servo_endianness_t endianness;

    /* Extra attempts after a failed one. 0 means try once and report. */
    uint8_t max_retries;

    /* 0 selects SERVO_BUS_DEFAULT_RESPONSE_TIMEOUT_US. */
    uint32_t response_timeout_us;
} servo_bus_config_t;

typedef struct {
    half_duplex_uart_t *uart;
    servo_endianness_t endianness;
    uint8_t max_retries;
    uint32_t response_timeout_us;
    servo_bus_stats_t stats;
    bool initialised;
} servo_bus_t;

servo_bus_result_t servo_bus_init(servo_bus_t *bus, const servo_bus_config_t *config);

/* ---------------------------------------------------------------------------
 * Transactions
 *
 * `error_out` may be NULL. When non-NULL it receives the servo's own error
 * byte — the SERVO_ERROR_* bits — and is set to 0 on any path that did not
 * reach a valid reply.
 *
 * Addressing SERVO_PROTOCOL_BROADCAST_ID sends without waiting for a reply,
 * since no servo answers a broadcast. Reads to the broadcast ID are rejected.
 * -------------------------------------------------------------------------*/

/* Is a servo at this ID? */
servo_bus_result_t servo_bus_ping(servo_bus_t *bus, uint8_t id, uint8_t *error_out);

servo_bus_result_t servo_bus_read(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                  uint8_t *data, uint8_t count, uint8_t *error_out);

servo_bus_result_t servo_bus_write(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                   const uint8_t *data, uint8_t count,
                                   uint8_t *error_out);

/* Read a 1-, 2- or 4-byte register, decoded in the bus's byte order. */
servo_bus_result_t servo_bus_read_value(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                        uint8_t width, uint32_t *value,
                                        uint8_t *error_out);

servo_bus_result_t servo_bus_write_value(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                         uint32_t value, uint8_t width,
                                         uint8_t *error_out);

/*
 * Write a different value to the same register on many servos, in one packet.
 *
 * Use this rather than a write per servo whenever they should move together.
 * Separate writes take a transaction each, so the first servo has begun moving
 * before the last has been told anything — several milliseconds of skew across
 * ten servos, which reads as a limb that does not move as one.
 *
 * Sent to the broadcast id, so nothing replies and nothing is acknowledged.
 * There is no retry either: a corrupted sync-write is simply not acted on, and
 * the way to find out is to read the positions back. That is the trade the
 * format makes for the timing.
 */
servo_bus_result_t servo_bus_sync_write(servo_bus_t *bus, uint8_t reg, uint8_t width,
                                        const servo_sync_target_t *targets,
                                        uint8_t count);

/* ---------------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------------*/

const servo_bus_stats_t *servo_bus_get_stats(const servo_bus_t *bus);
void servo_bus_reset_stats(servo_bus_t *bus);

/* Human-readable name for a result code. Never NULL. */
const char *servo_bus_result_name(servo_bus_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_SERVO_BUS_H */
