/*
 * feetech_registers - the Feetech STS/SMS control table.
 *
 * Feetech servos speak the same wire protocol as a Dynamixel AX-12 — the same
 * header, the same length rule, the same checksum — but their control table is
 * entirely different, and the two families disagree about byte order. Those
 * two facts are the whole of this component; the packet work is shared through
 * servo_bus.
 *
 * No Pico SDK dependency. Unit-tested on the host.
 *
 * Addresses follow the STS series (STS3215, STS3032, STS2032). SCS servos use
 * a different table again and are not covered here.
 */

#ifndef PICO_FRAMEWORK_FEETECH_REGISTERS_H
#define PICO_FRAMEWORK_FEETECH_REGISTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which family a servo belongs to, because it decides byte order.
 *
 * STS and SMS store multi-byte registers low byte first, like a Dynamixel.
 * SCS stores them high byte first. Read an SCS position with the STS order and
 * you get a number in range that moves the servo somewhere plausible and
 * wrong, so this is a choice the caller must make rather than a default.
 */
typedef enum {
    FEETECH_MODEL_STS = 0,  /* STS and SMS series: little-endian */
    FEETECH_MODEL_SCS = 1,  /* SCS series: big-endian */
} feetech_model_t;

/* EEPROM */
#define FEETECH_REG_MODEL              0x03
#define FEETECH_REG_ID                 0x05
#define FEETECH_REG_BAUD_RATE          0x06
#define FEETECH_REG_RETURN_DELAY       0x07
#define FEETECH_REG_STATUS_RETURN      0x08
#define FEETECH_REG_MIN_ANGLE_LIMIT    0x09
#define FEETECH_REG_MAX_ANGLE_LIMIT    0x0B
#define FEETECH_REG_MAX_TEMPERATURE    0x0D
#define FEETECH_REG_MAX_VOLTAGE        0x0E
#define FEETECH_REG_MIN_VOLTAGE        0x0F
#define FEETECH_REG_MAX_TORQUE         0x10
#define FEETECH_REG_SETTING_BYTE       0x12
#define FEETECH_REG_PROTECTION         0x13
#define FEETECH_REG_ALARM_LED          0x14
#define FEETECH_REG_P_GAIN             0x15
#define FEETECH_REG_D_GAIN             0x16
#define FEETECH_REG_I_GAIN             0x17
#define FEETECH_REG_MIN_START_TORQUE   0x18
#define FEETECH_REG_CW_DEAD_BAND       0x1A
#define FEETECH_REG_CCW_DEAD_BAND      0x1B
#define FEETECH_REG_OVERLOAD_CURRENT   0x1C
#define FEETECH_REG_RESOLUTION         0x1E
#define FEETECH_REG_POSITION_OFFSET    0x1F
#define FEETECH_REG_MODE               0x21

/* RAM */
#define FEETECH_REG_TORQUE_ENABLE      0x28
#define FEETECH_REG_ACCELERATION       0x29
#define FEETECH_REG_GOAL_POSITION      0x2A
#define FEETECH_REG_GOAL_TIME          0x2C
#define FEETECH_REG_GOAL_SPEED         0x2E
#define FEETECH_REG_TORQUE_LIMIT       0x30
#define FEETECH_REG_LOCK               0x37
#define FEETECH_REG_PRESENT_POSITION   0x38
#define FEETECH_REG_PRESENT_SPEED      0x3A
#define FEETECH_REG_PRESENT_LOAD       0x3C
#define FEETECH_REG_PRESENT_VOLTAGE    0x3E
#define FEETECH_REG_PRESENT_TEMPERATURE 0x3F
#define FEETECH_REG_MOVING             0x42
#define FEETECH_REG_PRESENT_CURRENT    0x45

/* STS servos have 12-bit position feedback across a full turn. */
#define FEETECH_POSITION_MIN 0u
#define FEETECH_POSITION_MAX 4095u
#define FEETECH_RANGE_MILLIDEGREES 360000u

#define FEETECH_SPEED_MAX 4095u

#define FEETECH_ID_MIN 0u
#define FEETECH_ID_MAX 253u

/* Values for FEETECH_REG_LOCK. The EEPROM must be unlocked before an ID or
   baud rate change will stick. */
#define FEETECH_LOCK_OPEN  0u
#define FEETECH_LOCK_CLOSED 1u

/*
 * Values for FEETECH_REG_BAUD_RATE. The register holds an index into a fixed
 * table, not a rate, and the change takes effect as the write lands.
 */
#define FEETECH_BAUD_INDEX_1000000 0u
#define FEETECH_BAUD_INDEX_500000  1u
#define FEETECH_BAUD_INDEX_250000  2u
#define FEETECH_BAUD_INDEX_128000  3u
#define FEETECH_BAUD_INDEX_115200  4u
#define FEETECH_BAUD_INDEX_76800   5u
#define FEETECH_BAUD_INDEX_57600   6u
#define FEETECH_BAUD_INDEX_38400   7u
#define FEETECH_BAUD_INDEX_MAX     7u

/* Values for FEETECH_REG_MODE. */
#define FEETECH_MODE_POSITION 0u
#define FEETECH_MODE_SPEED    1u

/* Width in bytes of the register at `address`, or 0 when it is not a
   documented register start. */
uint8_t feetech_register_width(uint8_t address);

const char *feetech_register_name(uint8_t address);

bool feetech_register_is_eeprom(uint8_t address);

/* Byte order for a model family. */
static inline servo_endianness_t feetech_endianness(feetech_model_t model)
{
    return (model == FEETECH_MODEL_SCS) ? SERVO_ENDIAN_BIG : SERVO_ENDIAN_LITTLE;
}

/* ---------------------------------------------------------------------------
 * Conversions
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Baud rate
 * -------------------------------------------------------------------------*/

/* Bus rate for a FEETECH_BAUD_INDEX_* value, or 0 if the index is unknown. */
uint32_t feetech_baud_index_to_rate(uint8_t index);

/*
 * The index that gives `rate`, within 3% so a nearby request still resolves.
 * Returns false and leaves `*index` alone when nothing in the table is close.
 */
bool feetech_rate_to_baud_index(uint32_t rate, uint8_t *index);

/*
 * The whole table, fastest first. `count` is always written. For sweeping a
 * bus whose speed is unknown.
 */
const uint32_t *feetech_baud_rate_table(size_t *count);

uint32_t feetech_position_to_millidegrees(uint16_t position);
uint16_t feetech_millidegrees_to_position(uint32_t millidegrees);

/*
 * Present speed, load and current use bit 15 as a direction flag with the
 * magnitude in the low 15 bits — a wider field than the AX-12's, and a
 * different bit, which is why this is not shared.
 */
int16_t feetech_decode_signed_magnitude(uint16_t raw);

/* Present voltage is reported in tenths of a volt. */
static inline uint32_t feetech_voltage_to_millivolts(uint8_t raw)
{
    return (uint32_t)raw * 100u;
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FEETECH_REGISTERS_H */
