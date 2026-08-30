#include <stddef.h>

#include "ax12_registers.h"

/*
 * One table drives width, name and EEPROM status, so the three cannot drift
 * apart the way three parallel switch statements would.
 */
typedef struct {
    uint8_t address;
    uint8_t width;
    bool eeprom;
    const char *name;
} register_info_t;

static const register_info_t g_registers[] = {
    { AX12_REG_MODEL_NUMBER,          2, true,  "model_number" },
    { AX12_REG_FIRMWARE_VERSION,      1, true,  "firmware_version" },
    { AX12_REG_ID,                    1, true,  "id" },
    { AX12_REG_BAUD_RATE,             1, true,  "baud_rate" },
    { AX12_REG_RETURN_DELAY_TIME,     1, true,  "return_delay_time" },
    { AX12_REG_CW_ANGLE_LIMIT,        2, true,  "cw_angle_limit" },
    { AX12_REG_CCW_ANGLE_LIMIT,       2, true,  "ccw_angle_limit" },
    { AX12_REG_TEMPERATURE_LIMIT,     1, true,  "temperature_limit" },
    { AX12_REG_MIN_VOLTAGE_LIMIT,     1, true,  "min_voltage_limit" },
    { AX12_REG_MAX_VOLTAGE_LIMIT,     1, true,  "max_voltage_limit" },
    { AX12_REG_MAX_TORQUE,            2, true,  "max_torque" },
    { AX12_REG_STATUS_RETURN_LEVEL,   1, true,  "status_return_level" },
    { AX12_REG_ALARM_LED,             1, true,  "alarm_led" },
    { AX12_REG_SHUTDOWN,              1, true,  "shutdown" },
    { AX12_REG_DOWN_CALIBRATION,      2, true,  "down_calibration" },
    { AX12_REG_UP_CALIBRATION,        2, true,  "up_calibration" },

    { AX12_REG_TORQUE_ENABLE,         1, false, "torque_enable" },
    { AX12_REG_LED,                   1, false, "led" },
    { AX12_REG_CW_COMPLIANCE_MARGIN,  1, false, "cw_compliance_margin" },
    { AX12_REG_CCW_COMPLIANCE_MARGIN, 1, false, "ccw_compliance_margin" },
    { AX12_REG_CW_COMPLIANCE_SLOPE,   1, false, "cw_compliance_slope" },
    { AX12_REG_CCW_COMPLIANCE_SLOPE,  1, false, "ccw_compliance_slope" },
    { AX12_REG_GOAL_POSITION,         2, false, "goal_position" },
    { AX12_REG_MOVING_SPEED,          2, false, "moving_speed" },
    { AX12_REG_TORQUE_LIMIT,          2, false, "torque_limit" },
    { AX12_REG_PRESENT_POSITION,      2, false, "present_position" },
    { AX12_REG_PRESENT_SPEED,         2, false, "present_speed" },
    { AX12_REG_PRESENT_LOAD,          2, false, "present_load" },
    { AX12_REG_PRESENT_VOLTAGE,       1, false, "present_voltage" },
    { AX12_REG_PRESENT_TEMPERATURE,   1, false, "present_temperature" },
    { AX12_REG_REGISTERED,            1, false, "registered" },
    { AX12_REG_MOVING,                1, false, "moving" },
    { AX12_REG_LOCK,                  1, false, "lock" },
    { AX12_REG_PUNCH,                 2, false, "punch" },
};

static const register_info_t *lookup(uint8_t address)
{
    for (size_t i = 0; i < sizeof(g_registers) / sizeof(g_registers[0]); i++) {
        if (g_registers[i].address == address) {
            return &g_registers[i];
        }
    }
    return NULL;
}

uint8_t ax12_register_width(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL ? info->width : 0;
}

const char *ax12_register_name(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL ? info->name : NULL;
}

bool ax12_register_is_eeprom(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL && info->eeprom;
}

uint32_t ax12_position_to_millidegrees(uint16_t position)
{
    if (position > AX12_POSITION_MAX) {
        position = AX12_POSITION_MAX;
    }

    /* Rounded so the conversion back recovers the same count. */
    const uint32_t numerator =
        (uint32_t)position * AX12_RANGE_MILLIDEGREES + AX12_POSITION_MAX / 2u;
    return numerator / AX12_POSITION_MAX;
}

uint16_t ax12_millidegrees_to_position(uint32_t millidegrees)
{
    if (millidegrees >= AX12_RANGE_MILLIDEGREES) {
        return AX12_POSITION_MAX;
    }

    const uint64_t numerator = (uint64_t)millidegrees * AX12_POSITION_MAX +
                               AX12_RANGE_MILLIDEGREES / 2u;
    return (uint16_t)(numerator / AX12_RANGE_MILLIDEGREES);
}

int16_t ax12_decode_signed_magnitude(uint16_t raw)
{
    const uint16_t magnitude = raw & 0x03FFu;
    const bool clockwise = (raw & 0x0400u) != 0;
    return clockwise ? (int16_t)(-(int32_t)magnitude) : (int16_t)magnitude;
}
