/*
 * ax12_registers - the AX-12 control table, and the conversions that go with it.
 *
 * No Pico SDK dependency and no transport: register addresses, their widths,
 * and unit conversions. Unit-tested on the host.
 *
 * The width table matters more than it looks. Reading a two-byte register one
 * byte at a time returns the low half and looks like a plausible value, so a
 * wrong width is a silent wrong answer rather than a failure.
 */

#ifndef PICO_FRAMEWORK_AX12_REGISTERS_H
#define PICO_FRAMEWORK_AX12_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EEPROM: survives power-off, and has a limited write endurance. */
#define AX12_REG_MODEL_NUMBER          0x00
#define AX12_REG_FIRMWARE_VERSION      0x02
#define AX12_REG_ID                    0x03
#define AX12_REG_BAUD_RATE             0x04
#define AX12_REG_RETURN_DELAY_TIME     0x05
#define AX12_REG_CW_ANGLE_LIMIT        0x06
#define AX12_REG_CCW_ANGLE_LIMIT       0x08
#define AX12_REG_TEMPERATURE_LIMIT     0x0B
#define AX12_REG_MIN_VOLTAGE_LIMIT     0x0C
#define AX12_REG_MAX_VOLTAGE_LIMIT     0x0D
#define AX12_REG_MAX_TORQUE            0x0E
#define AX12_REG_STATUS_RETURN_LEVEL   0x10
#define AX12_REG_ALARM_LED             0x11
#define AX12_REG_SHUTDOWN              0x12
#define AX12_REG_DOWN_CALIBRATION      0x14
#define AX12_REG_UP_CALIBRATION        0x16

/* RAM: resets to defaults on power-up. */
#define AX12_REG_TORQUE_ENABLE         0x18
#define AX12_REG_LED                   0x19
#define AX12_REG_CW_COMPLIANCE_MARGIN  0x1A
#define AX12_REG_CCW_COMPLIANCE_MARGIN 0x1B
#define AX12_REG_CW_COMPLIANCE_SLOPE   0x1C
#define AX12_REG_CCW_COMPLIANCE_SLOPE  0x1D
#define AX12_REG_GOAL_POSITION         0x1E
#define AX12_REG_MOVING_SPEED          0x20
#define AX12_REG_TORQUE_LIMIT          0x22
#define AX12_REG_PRESENT_POSITION      0x24
#define AX12_REG_PRESENT_SPEED         0x26
#define AX12_REG_PRESENT_LOAD          0x28
#define AX12_REG_PRESENT_VOLTAGE       0x2A
#define AX12_REG_PRESENT_TEMPERATURE   0x2B
#define AX12_REG_REGISTERED            0x2C
#define AX12_REG_MOVING                0x2E
#define AX12_REG_LOCK                  0x2F
#define AX12_REG_PUNCH                 0x30

/* Position range and the arc it covers. An AX-12 turns 300 degrees. */
#define AX12_POSITION_MIN 0u
#define AX12_POSITION_MAX 1023u
#define AX12_RANGE_MILLIDEGREES 300000u

/* Speed 0 is a special case: it means "no limit", not "stop". */
#define AX12_SPEED_MAX 1023u
#define AX12_SPEED_UNLIMITED 0u

/* Lowest ID a servo can be given; 0xFE is the broadcast address. */
#define AX12_ID_MIN 0u
#define AX12_ID_MAX 253u

/*
 * Width in bytes of the register at `address`, or 0 when the address is not a
 * documented register start. Addressing the high half of a two-byte register
 * returns 0 rather than 1, so a typo is caught instead of silently reading
 * half a value.
 */
uint8_t ax12_register_width(uint8_t address);

/* Human-readable register name, or NULL when the address is not a known one. */
const char *ax12_register_name(uint8_t address);

/* True when writing to this register wears the EEPROM. */
bool ax12_register_is_eeprom(uint8_t address);

/* ---------------------------------------------------------------------------
 * Conversions
 *
 * Angles are in millidegrees throughout, as integers: the servo's own
 * resolution is about 293 millidegrees per step, so nothing is lost, and
 * integer maths keeps these exactly testable.
 * -------------------------------------------------------------------------*/

/* Position count to angle. 0 maps to 0, 1023 maps to 300000. */
uint32_t ax12_position_to_millidegrees(uint16_t position);

/* Angle to the nearest position count, clamped into range. */
uint16_t ax12_millidegrees_to_position(uint32_t millidegrees);

/*
 * Present speed and present load share an encoding: bits 0..9 are the
 * magnitude and bit 10 is the direction, set for clockwise. Returns a signed
 * value, negative for clockwise, so the two can be compared and plotted.
 */
int16_t ax12_decode_signed_magnitude(uint16_t raw);

/* Present voltage is reported in tenths of a volt. */
static inline uint32_t ax12_voltage_to_millivolts(uint8_t raw)
{
    return (uint32_t)raw * 100u;
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_AX12_REGISTERS_H */
