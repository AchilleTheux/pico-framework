#include <string.h>

#include "servo_bus.h"

servo_bus_result_t servo_bus_init(servo_bus_t *bus, const servo_bus_config_t *config)
{
    if (bus == NULL || config == NULL || config->uart == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    *bus = (servo_bus_t){
        .uart = config->uart,
        .endianness = config->endianness,
        .max_retries = config->max_retries,
        .response_timeout_us = config->response_timeout_us != 0
            ? config->response_timeout_us
            : SERVO_BUS_DEFAULT_RESPONSE_TIMEOUT_US,
        .initialised = true,
    };

    return SERVO_BUS_OK;
}

/*
 * Time to allow for a reply of `reply_size` bytes: how long those bytes take
 * on the wire at the current rate, plus the servo's turnaround allowance.
 * Deriving the first half from the baud rate means changing the bus speed
 * cannot quietly leave the timeout too tight.
 */
static uint32_t reply_timeout_us(const servo_bus_t *bus, size_t reply_size)
{
    const uint32_t transmission =
        half_duplex_uart_frame_time_us(bus->uart->baudrate, (uint32_t)reply_size);
    return transmission + bus->response_timeout_us;
}

static servo_bus_result_t transport_error(half_duplex_uart_result_t result)
{
    return result == HALF_DUPLEX_UART_ERR_TIMEOUT ? SERVO_BUS_ERR_TIMEOUT
                                                  : SERVO_BUS_ERR_TRANSPORT;
}

/*
 * One attempt: send the packet, and unless this is a broadcast, read and
 * validate the reply. Retrying is the caller's job, so this stays free of
 * policy.
 */
static servo_bus_result_t attempt(servo_bus_t *bus,
                                  const uint8_t *request, size_t request_len,
                                  uint8_t id,
                                  uint8_t *params_out, uint8_t param_count,
                                  uint8_t *error_out)
{
    const half_duplex_uart_result_t sent =
        half_duplex_uart_write(bus->uart, request, request_len);
    if (sent != HALF_DUPLEX_UART_OK) {
        return transport_error(sent);
    }

    if (id == SERVO_PROTOCOL_BROADCAST_ID) {
        return SERVO_BUS_OK; /* nobody answers a broadcast */
    }

    uint8_t reply[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    const size_t expected = servo_protocol_packet_size(param_count);
    if (expected > sizeof(reply)) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    const half_duplex_uart_result_t received =
        half_duplex_uart_read_exact(bus->uart, reply, expected,
                                    reply_timeout_us(bus, expected));
    if (received != HALF_DUPLEX_UART_OK) {
        return transport_error(received);
    }

    servo_status_packet_t status;
    const servo_protocol_result_t parsed =
        servo_protocol_parse_status(reply, expected, &status);

    switch (parsed) {
        case SERVO_PROTOCOL_OK:
            break;
        case SERVO_PROTOCOL_ERR_BAD_CHECKSUM:
            return SERVO_BUS_ERR_CHECKSUM;
        case SERVO_PROTOCOL_ERR_INCOMPLETE:
            /* We read exactly as many bytes as a well-formed reply needs, so
               a short packet here means the LENGTH field disagreed. */
            return SERVO_BUS_ERR_MALFORMED;
        default:
            return SERVO_BUS_ERR_MALFORMED;
    }

    if (status.id != id) {
        return SERVO_BUS_ERR_WRONG_ID;
    }
    if (status.param_count < param_count) {
        return SERVO_BUS_ERR_SHORT_REPLY;
    }

    if (params_out != NULL && param_count > 0) {
        memcpy(params_out, status.params, param_count);
    }
    if (error_out != NULL) {
        *error_out = status.error;
    }

    return SERVO_BUS_OK;
}

/* Run `attempt` until it succeeds or the retry budget runs out, keeping the
   statistics as it goes. */
static servo_bus_result_t transact(servo_bus_t *bus,
                                   const uint8_t *request, size_t request_len,
                                   uint8_t id,
                                   uint8_t *params_out, uint8_t param_count,
                                   uint8_t *error_out)
{
    if (error_out != NULL) {
        *error_out = 0;
    }

    servo_bus_result_t result = SERVO_BUS_ERR_TIMEOUT;

    for (unsigned try = 0; try <= bus->max_retries; try++) {
        if (try > 0) {
            bus->stats.retries++;
        }
        bus->stats.transactions++;

        result = attempt(bus, request, request_len, id,
                         params_out, param_count, error_out);
        if (result == SERVO_BUS_OK) {
            return SERVO_BUS_OK;
        }

        switch (result) {
            case SERVO_BUS_ERR_TIMEOUT:     bus->stats.timeouts++; break;
            case SERVO_BUS_ERR_CHECKSUM:    bus->stats.checksum_errors++; break;
            case SERVO_BUS_ERR_WRONG_ID:    bus->stats.wrong_id++; break;
            case SERVO_BUS_ERR_MALFORMED:
            case SERVO_BUS_ERR_SHORT_REPLY: bus->stats.malformed++; break;
            default: break;
        }

        /* A bad argument will fail identically every time. */
        if (result == SERVO_BUS_ERR_INVALID_ARG) {
            return result;
        }

        /* Whatever confused the last exchange may still be arriving; start the
           retry from a quiet line rather than reading a stale tail. */
        half_duplex_uart_flush_rx(bus->uart);
    }

    return result;
}

/* ---------------------------------------------------------------------------
 * Transactions
 * -------------------------------------------------------------------------*/

servo_bus_result_t servo_bus_ping(servo_bus_t *bus, uint8_t id, uint8_t *error_out)
{
    if (bus == NULL || !bus->initialised) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint8_t request[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t request_len = 0;

    if (servo_protocol_build_ping(request, sizeof(request), &request_len, id)
            != SERVO_PROTOCOL_OK) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    return transact(bus, request, request_len, id, NULL, 0, error_out);
}

servo_bus_result_t servo_bus_read(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                  uint8_t *data, uint8_t count, uint8_t *error_out)
{
    if (bus == NULL || !bus->initialised || data == NULL || count == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    /* A broadcast read would have every servo answer at once. */
    if (id == SERVO_PROTOCOL_BROADCAST_ID) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint8_t request[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t request_len = 0;

    if (servo_protocol_build_read(request, sizeof(request), &request_len, id, reg, count)
            != SERVO_PROTOCOL_OK) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    return transact(bus, request, request_len, id, data, count, error_out);
}

servo_bus_result_t servo_bus_write(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                   const uint8_t *data, uint8_t count,
                                   uint8_t *error_out)
{
    if (bus == NULL || !bus->initialised || data == NULL || count == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint8_t request[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t request_len = 0;

    if (servo_protocol_build_write(request, sizeof(request), &request_len,
                                   id, reg, data, count) != SERVO_PROTOCOL_OK) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    /* A write is acknowledged with a status packet carrying no parameters. */
    return transact(bus, request, request_len, id, NULL, 0, error_out);
}

servo_bus_result_t servo_bus_read_value(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                        uint8_t width, uint32_t *value,
                                        uint8_t *error_out)
{
    if (value == NULL || (width != 1 && width != 2 && width != 4)) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint8_t data[4];
    const servo_bus_result_t result =
        servo_bus_read(bus, id, reg, data, width, error_out);
    if (result != SERVO_BUS_OK) {
        return result;
    }

    *value = servo_protocol_decode_value(data, width, bus->endianness);
    return SERVO_BUS_OK;
}

servo_bus_result_t servo_bus_write_value(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                         uint32_t value, uint8_t width,
                                         uint8_t *error_out)
{
    if (bus == NULL || !bus->initialised ||
        (width != 1 && width != 2 && width != 4)) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint8_t encoded[4];
    servo_protocol_encode_value(encoded, value, width, bus->endianness);

    return servo_bus_write(bus, id, reg, encoded, width, error_out);
}

/* ---------------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------------*/

const servo_bus_stats_t *servo_bus_get_stats(const servo_bus_t *bus)
{
    return (bus != NULL && bus->initialised) ? &bus->stats : NULL;
}

void servo_bus_reset_stats(servo_bus_t *bus)
{
    if (bus != NULL && bus->initialised) {
        bus->stats = (servo_bus_stats_t){ 0 };
    }
}

const char *servo_bus_result_name(servo_bus_result_t result)
{
    switch (result) {
        case SERVO_BUS_OK:                return "ok";
        case SERVO_BUS_ERR_INVALID_ARG:   return "invalid argument";
        case SERVO_BUS_ERR_TRANSPORT:     return "transport failure";
        case SERVO_BUS_ERR_TIMEOUT:       return "timeout";
        case SERVO_BUS_ERR_CHECKSUM:      return "bad checksum";
        case SERVO_BUS_ERR_MALFORMED:     return "malformed reply";
        case SERVO_BUS_ERR_WRONG_ID:      return "reply from another id";
        case SERVO_BUS_ERR_SHORT_REPLY:   return "short reply";
        default:                          return "unknown";
    }
}
