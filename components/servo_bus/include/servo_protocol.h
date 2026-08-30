/*
 * servo_protocol - packet encoding and decoding for Dynamixel Protocol 1.0.
 *
 * The wire format spoken by Dynamixel AX-12 servos and, byte for byte, by
 * Feetech STS, SMS and SCS servos too. Keeping it in one place is the reason
 * the `ax12` and `feetech` components are thin: they differ in their register
 * maps and in one endianness choice, not in how a packet is built.
 *
 * Pure integer and buffer work with no Pico SDK dependency, so all of it is
 * unit-tested on the host. The transport lives in servo_bus.h.
 *
 * Instruction packet:
 *
 *   0xFF 0xFF  ID  LENGTH  INSTRUCTION  PARAM...  CHECKSUM
 *
 * Status packet:
 *
 *   0xFF 0xFF  ID  LENGTH  ERROR        PARAM...  CHECKSUM
 *
 * LENGTH counts the bytes after it: parameters plus the instruction/error byte
 * plus the checksum, so the packet is LENGTH + 4 bytes long. CHECKSUM is the
 * one's complement of the sum of everything from ID up to the last parameter.
 */

#ifndef PICO_FRAMEWORK_SERVO_PROTOCOL_H
#define PICO_FRAMEWORK_SERVO_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every servo acts on a packet sent to this ID, and none of them answers it. */
#define SERVO_PROTOCOL_BROADCAST_ID 0xFEu

/* Largest parameter count this implementation encodes or decodes. Generous for
   single-register access; a sync-write to many servos would need more. */
#ifndef SERVO_PROTOCOL_MAX_PARAMS
#define SERVO_PROTOCOL_MAX_PARAMS 32u
#endif

/* 0xFF 0xFF ID LENGTH + instruction/error + params + checksum */
#define SERVO_PROTOCOL_OVERHEAD 6u
#define SERVO_PROTOCOL_MAX_PACKET_SIZE \
    (SERVO_PROTOCOL_OVERHEAD + SERVO_PROTOCOL_MAX_PARAMS)

/* Smallest legal packet: no parameters at all. */
#define SERVO_PROTOCOL_MIN_PACKET_SIZE SERVO_PROTOCOL_OVERHEAD

typedef enum {
    SERVO_INST_PING = 0x01,
    SERVO_INST_READ = 0x02,
    SERVO_INST_WRITE = 0x03,
    SERVO_INST_REG_WRITE = 0x04,
    SERVO_INST_ACTION = 0x05,
    SERVO_INST_RESET = 0x06,
    SERVO_INST_SYNC_WRITE = 0x83,
} servo_instruction_t;

/*
 * Byte order of multi-byte register values.
 *
 * Dynamixel AX-12 and Feetech STS/SMS put the low byte first. Feetech SCS
 * servos put the high byte first, and reading one with the wrong order gives
 * plausible-looking nonsense rather than an obvious failure, which is why this
 * is an explicit parameter and not an assumption.
 */
typedef enum {
    SERVO_ENDIAN_LITTLE = 0,
    SERVO_ENDIAN_BIG = 1,
} servo_endianness_t;

/* Bits of the status packet's error byte. */
#define SERVO_ERROR_INPUT_VOLTAGE (1u << 0)
#define SERVO_ERROR_ANGLE_LIMIT   (1u << 1)
#define SERVO_ERROR_OVERHEATING   (1u << 2)
#define SERVO_ERROR_RANGE         (1u << 3)
#define SERVO_ERROR_CHECKSUM      (1u << 4)
#define SERVO_ERROR_OVERLOAD      (1u << 5)
#define SERVO_ERROR_INSTRUCTION   (1u << 6)

typedef enum {
    SERVO_PROTOCOL_OK = 0,
    SERVO_PROTOCOL_ERR_INVALID_ARG,
    SERVO_PROTOCOL_ERR_BUFFER_TOO_SMALL,
    SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS,
    SERVO_PROTOCOL_ERR_BAD_HEADER,    /* no 0xFF 0xFF, or an impossible LENGTH */
    SERVO_PROTOCOL_ERR_INCOMPLETE,    /* well-formed so far, but truncated */
    SERVO_PROTOCOL_ERR_BAD_CHECKSUM,
} servo_protocol_result_t;

typedef struct {
    uint8_t id;
    uint8_t error;           /* the SERVO_ERROR_* bits the servo reported */
    const uint8_t *params;   /* points into the caller's buffer, not a copy */
    uint8_t param_count;
    size_t packet_size;      /* bytes consumed, so a caller can advance */
} servo_status_packet_t;

/* ---------------------------------------------------------------------------
 * Sizes
 * -------------------------------------------------------------------------*/

/* Bytes on the wire for a packet carrying `param_count` parameters. */
static inline size_t servo_protocol_packet_size(uint8_t param_count)
{
    return (size_t)param_count + SERVO_PROTOCOL_OVERHEAD;
}

/*
 * Bytes a servo will send back after a READ of `count` registers. The reply
 * carries the requested bytes as its parameters.
 */
static inline size_t servo_protocol_read_reply_size(uint8_t count)
{
    return servo_protocol_packet_size(count);
}

/* ---------------------------------------------------------------------------
 * Checksum
 * -------------------------------------------------------------------------*/

/*
 * One's complement of the sum of `len` bytes starting at `data`, which for a
 * whole packet means starting at the ID and ending at the last parameter.
 */
uint8_t servo_protocol_checksum(const uint8_t *data, size_t len);

/* ---------------------------------------------------------------------------
 * Building instruction packets
 *
 * Each writes into `out` and reports the byte count through `written`, which
 * is set to 0 on failure. `params` may be NULL when `param_count` is 0.
 * -------------------------------------------------------------------------*/

servo_protocol_result_t servo_protocol_build(uint8_t *out, size_t capacity,
                                             size_t *written,
                                             uint8_t id, uint8_t instruction,
                                             const uint8_t *params,
                                             uint8_t param_count);

servo_protocol_result_t servo_protocol_build_ping(uint8_t *out, size_t capacity,
                                                  size_t *written, uint8_t id);

/* Ask `id` for `count` bytes starting at register `reg`. */
servo_protocol_result_t servo_protocol_build_read(uint8_t *out, size_t capacity,
                                                  size_t *written,
                                                  uint8_t id, uint8_t reg,
                                                  uint8_t count);

/* Write raw bytes to consecutive registers starting at `reg`. */
servo_protocol_result_t servo_protocol_build_write(uint8_t *out, size_t capacity,
                                                   size_t *written,
                                                   uint8_t id, uint8_t reg,
                                                   const uint8_t *data,
                                                   uint8_t count);

/*
 * Write a 1-, 2- or 4-byte value, encoded in the given byte order. The common
 * case: goal position, speed, torque enable.
 */
servo_protocol_result_t servo_protocol_build_write_value(uint8_t *out, size_t capacity,
                                                         size_t *written,
                                                         uint8_t id, uint8_t reg,
                                                         uint32_t value, uint8_t width,
                                                         servo_endianness_t endianness);

/* ---------------------------------------------------------------------------
 * Parsing status packets
 * -------------------------------------------------------------------------*/

/*
 * Validate a status packet at the start of `data` and describe it in `out`.
 *
 * Returns SERVO_PROTOCOL_ERR_INCOMPLETE when the buffer holds a well-formed
 * prefix but not the whole packet, which lets a caller tell "keep waiting"
 * from "this is not a packet".
 *
 * `out->params` points into `data`; nothing is copied.
 */
servo_protocol_result_t servo_protocol_parse_status(const uint8_t *data, size_t len,
                                                    servo_status_packet_t *out);

/* ---------------------------------------------------------------------------
 * Values
 * -------------------------------------------------------------------------*/

/* Decode `width` bytes (1, 2 or 4) into a host value. Returns 0 for a bad width. */
uint32_t servo_protocol_decode_value(const uint8_t *data, uint8_t width,
                                     servo_endianness_t endianness);

/* Encode a host value into `width` bytes. Does nothing for a bad width. */
void servo_protocol_encode_value(uint8_t *out, uint32_t value, uint8_t width,
                                 servo_endianness_t endianness);

/* ---------------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------------*/

/* Human-readable name for a result code. Never NULL. */
const char *servo_protocol_result_name(servo_protocol_result_t result);

/*
 * Comma-separated names of the set bits in a status error byte, or "none".
 * Always NUL-terminates when capacity is non-zero. Returns `buffer`.
 */
const char *servo_protocol_describe_error(uint8_t error, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_SERVO_PROTOCOL_H */
