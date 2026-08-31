#include <string.h>

#include "servo_protocol.h"

#define HEADER_BYTE 0xFFu

/* Offsets within a packet. */
#define OFFSET_ID          2u
#define OFFSET_LENGTH      3u
#define OFFSET_INSTRUCTION 4u  /* also the ERROR byte of a status packet */
#define OFFSET_PARAMS      5u

/* LENGTH counts the instruction/error byte, the parameters and the checksum,
   so it is never below 2. */
#define LENGTH_MINIMUM 2u

uint8_t servo_protocol_checksum(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return 0;
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return (uint8_t)~sum;
}

servo_protocol_result_t servo_protocol_build(uint8_t *out, size_t capacity,
                                             size_t *written,
                                             uint8_t id, uint8_t instruction,
                                             const uint8_t *params,
                                             uint8_t param_count)
{
    if (written != NULL) {
        *written = 0;
    }
    if (out == NULL || (params == NULL && param_count > 0)) {
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }
    if (param_count > SERVO_PROTOCOL_MAX_PARAMS) {
        return SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS;
    }

    const size_t size = servo_protocol_packet_size(param_count);
    if (capacity < size) {
        return SERVO_PROTOCOL_ERR_BUFFER_TOO_SMALL;
    }

    out[0] = HEADER_BYTE;
    out[1] = HEADER_BYTE;
    out[OFFSET_ID] = id;
    out[OFFSET_LENGTH] = (uint8_t)(param_count + LENGTH_MINIMUM);
    out[OFFSET_INSTRUCTION] = instruction;

    if (param_count > 0) {
        memcpy(&out[OFFSET_PARAMS], params, param_count);
    }

    /* Checksum covers ID, LENGTH, INSTRUCTION and the parameters: everything
       between the header and the checksum itself. */
    out[size - 1] = servo_protocol_checksum(&out[OFFSET_ID], size - 1 - OFFSET_ID);

    if (written != NULL) {
        *written = size;
    }
    return SERVO_PROTOCOL_OK;
}

servo_protocol_result_t servo_protocol_build_ping(uint8_t *out, size_t capacity,
                                                  size_t *written, uint8_t id)
{
    return servo_protocol_build(out, capacity, written, id, SERVO_INST_PING, NULL, 0);
}

servo_protocol_result_t servo_protocol_build_read(uint8_t *out, size_t capacity,
                                                  size_t *written,
                                                  uint8_t id, uint8_t reg,
                                                  uint8_t count)
{
    if (count == 0) {
        if (written != NULL) {
            *written = 0;
        }
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }

    const uint8_t params[2] = { reg, count };
    return servo_protocol_build(out, capacity, written, id, SERVO_INST_READ,
                                params, (uint8_t)sizeof(params));
}

servo_protocol_result_t servo_protocol_build_write(uint8_t *out, size_t capacity,
                                                   size_t *written,
                                                   uint8_t id, uint8_t reg,
                                                   const uint8_t *data,
                                                   uint8_t count)
{
    if (written != NULL) {
        *written = 0;
    }
    if (data == NULL || count == 0) {
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }
    /* One parameter byte is spent on the register address. */
    if ((size_t)count + 1u > SERVO_PROTOCOL_MAX_PARAMS) {
        return SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS;
    }

    uint8_t params[SERVO_PROTOCOL_MAX_PARAMS];
    params[0] = reg;
    memcpy(&params[1], data, count);

    return servo_protocol_build(out, capacity, written, id, SERVO_INST_WRITE,
                                params, (uint8_t)(count + 1u));
}

servo_protocol_result_t servo_protocol_build_write_value(uint8_t *out, size_t capacity,
                                                         size_t *written,
                                                         uint8_t id, uint8_t reg,
                                                         uint32_t value, uint8_t width,
                                                         servo_endianness_t endianness)
{
    if (width != 1 && width != 2 && width != 4) {
        if (written != NULL) {
            *written = 0;
        }
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }

    uint8_t encoded[4];
    servo_protocol_encode_value(encoded, value, width, endianness);

    return servo_protocol_build_write(out, capacity, written, id, reg, encoded, width);
}

servo_protocol_result_t servo_protocol_build_sync_write(uint8_t *out, size_t capacity,
                                                       size_t *written,
                                                       uint8_t reg, uint8_t width,
                                                       const servo_sync_target_t *targets,
                                                       uint8_t count,
                                                       servo_endianness_t endianness)
{
    if (written != NULL) {
        *written = 0;
    }
    if (out == NULL || targets == NULL || count == 0) {
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }
    if (width != 1 && width != 2 && width != 4) {
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }

    const size_t param_count = servo_protocol_sync_write_params(width, count);
    if (param_count > SERVO_PROTOCOL_MAX_PARAMS) {
        return SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS;
    }

    /*
     * Parameters are the register, the width, and then each servo's id
     * followed by its value. The receiving servos use the width to know how to
     * split what follows, which is why it is on the wire rather than implied.
     */
    uint8_t params[SERVO_PROTOCOL_MAX_PARAMS];
    size_t at = 0;
    params[at++] = reg;
    params[at++] = width;

    for (uint8_t i = 0; i < count; i++) {
        params[at++] = targets[i].id;
        servo_protocol_encode_value(&params[at], targets[i].value, width, endianness);
        at += width;
    }

    return servo_protocol_build(out, capacity, written, SERVO_PROTOCOL_BROADCAST_ID,
                               SERVO_INST_SYNC_WRITE, params, (uint8_t)at);
}

servo_protocol_result_t servo_protocol_parse_status(const uint8_t *data, size_t len,
                                                    servo_status_packet_t *out)
{
    if (data == NULL || out == NULL) {
        return SERVO_PROTOCOL_ERR_INVALID_ARG;
    }

    *out = (servo_status_packet_t){ 0 };

    /*
     * Not enough to judge yet. Reported as INCOMPLETE rather than BAD_HEADER
     * so a caller reading a byte at a time can tell "wait for more" from
     * "this will never be a packet".
     */
    if (len < 2) {
        return SERVO_PROTOCOL_ERR_INCOMPLETE;
    }
    if (data[0] != HEADER_BYTE || data[1] != HEADER_BYTE) {
        return SERVO_PROTOCOL_ERR_BAD_HEADER;
    }
    if (len <= OFFSET_LENGTH) {
        return SERVO_PROTOCOL_ERR_INCOMPLETE;
    }

    const uint8_t length = data[OFFSET_LENGTH];
    if (length < LENGTH_MINIMUM) {
        return SERVO_PROTOCOL_ERR_BAD_HEADER;
    }

    const uint8_t param_count = (uint8_t)(length - LENGTH_MINIMUM);
    if (param_count > SERVO_PROTOCOL_MAX_PARAMS) {
        return SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS;
    }

    const size_t size = servo_protocol_packet_size(param_count);
    if (len < size) {
        return SERVO_PROTOCOL_ERR_INCOMPLETE;
    }

    const uint8_t expected = servo_protocol_checksum(&data[OFFSET_ID], size - 1 - OFFSET_ID);
    if (expected != data[size - 1]) {
        return SERVO_PROTOCOL_ERR_BAD_CHECKSUM;
    }

    out->id = data[OFFSET_ID];
    out->error = data[OFFSET_INSTRUCTION];
    out->params = param_count > 0 ? &data[OFFSET_PARAMS] : NULL;
    out->param_count = param_count;
    out->packet_size = size;

    return SERVO_PROTOCOL_OK;
}

uint32_t servo_protocol_decode_value(const uint8_t *data, uint8_t width,
                                     servo_endianness_t endianness)
{
    if (data == NULL || (width != 1 && width != 2 && width != 4)) {
        return 0;
    }

    uint32_t value = 0;
    for (uint8_t i = 0; i < width; i++) {
        const uint8_t byte = (endianness == SERVO_ENDIAN_LITTLE)
            ? data[i]
            : data[width - 1u - i];
        value |= (uint32_t)byte << (8u * i);
    }
    return value;
}

void servo_protocol_encode_value(uint8_t *out, uint32_t value, uint8_t width,
                                 servo_endianness_t endianness)
{
    if (out == NULL || (width != 1 && width != 2 && width != 4)) {
        return;
    }

    for (uint8_t i = 0; i < width; i++) {
        const uint8_t byte = (uint8_t)(value >> (8u * i));
        if (endianness == SERVO_ENDIAN_LITTLE) {
            out[i] = byte;
        } else {
            out[width - 1u - i] = byte;
        }
    }
}

const char *servo_protocol_result_name(servo_protocol_result_t result)
{
    switch (result) {
        case SERVO_PROTOCOL_OK:                  return "ok";
        case SERVO_PROTOCOL_ERR_INVALID_ARG:     return "invalid argument";
        case SERVO_PROTOCOL_ERR_BUFFER_TOO_SMALL: return "buffer too small";
        case SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS: return "too many parameters";
        case SERVO_PROTOCOL_ERR_BAD_HEADER:      return "bad header";
        case SERVO_PROTOCOL_ERR_INCOMPLETE:      return "incomplete";
        case SERVO_PROTOCOL_ERR_BAD_CHECKSUM:    return "bad checksum";
        default:                                 return "unknown";
    }
}

const char *servo_protocol_describe_error(uint8_t error, char *buffer, size_t capacity)
{
    static const struct {
        uint8_t bit;
        const char *name;
    } flags[] = {
        { SERVO_ERROR_INPUT_VOLTAGE, "voltage" },
        { SERVO_ERROR_ANGLE_LIMIT,   "angle limit" },
        { SERVO_ERROR_OVERHEATING,   "overheating" },
        { SERVO_ERROR_RANGE,         "range" },
        { SERVO_ERROR_CHECKSUM,      "checksum" },
        { SERVO_ERROR_OVERLOAD,      "overload" },
        { SERVO_ERROR_INSTRUCTION,   "instruction" },
    };

    if (buffer == NULL || capacity == 0) {
        return buffer;
    }

    size_t used = 0;
    buffer[0] = '\0';

    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
        if ((error & flags[i].bit) == 0) {
            continue;
        }

        const char *separator = (used > 0) ? ", " : "";
        const size_t needed = strlen(separator) + strlen(flags[i].name);

        /* Stop cleanly rather than truncating mid-word. */
        if (used + needed + 1 > capacity) {
            break;
        }

        memcpy(&buffer[used], separator, strlen(separator));
        used += strlen(separator);
        memcpy(&buffer[used], flags[i].name, strlen(flags[i].name));
        used += strlen(flags[i].name);
        buffer[used] = '\0';
    }

    if (used == 0) {
        const char *none = "none";
        const size_t len = (strlen(none) + 1 <= capacity) ? strlen(none) : capacity - 1;
        memcpy(buffer, none, len);
        buffer[len] = '\0';
    }

    return buffer;
}
