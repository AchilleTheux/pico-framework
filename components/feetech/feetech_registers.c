#include <stddef.h>

#include "feetech_registers.h"

typedef struct {
    uint8_t address;
    uint8_t width;
    bool eeprom;
    const char *name;
} register_info_t;

static const register_info_t g_registers[] = {
    { FEETECH_REG_MODEL,               2, true,  "model" },
    { FEETECH_REG_ID,                  1, true,  "id" },
    { FEETECH_REG_BAUD_RATE,           1, true,  "baud_rate" },
    { FEETECH_REG_RETURN_DELAY,        1, true,  "return_delay" },
    { FEETECH_REG_STATUS_RETURN,       1, true,  "status_return" },
    { FEETECH_REG_MIN_ANGLE_LIMIT,     2, true,  "min_angle_limit" },
    { FEETECH_REG_MAX_ANGLE_LIMIT,     2, true,  "max_angle_limit" },
    { FEETECH_REG_MAX_TEMPERATURE,     1, true,  "max_temperature" },
    { FEETECH_REG_MAX_VOLTAGE,         1, true,  "max_voltage" },
    { FEETECH_REG_MIN_VOLTAGE,         1, true,  "min_voltage" },
    { FEETECH_REG_MAX_TORQUE,          2, true,  "max_torque" },
    { FEETECH_REG_SETTING_BYTE,        1, true,  "setting_byte" },
    { FEETECH_REG_PROTECTION,          1, true,  "protection" },
    { FEETECH_REG_ALARM_LED,           1, true,  "alarm_led" },
    { FEETECH_REG_P_GAIN,              1, true,  "p_gain" },
    { FEETECH_REG_D_GAIN,              1, true,  "d_gain" },
    { FEETECH_REG_I_GAIN,              1, true,  "i_gain" },
    { FEETECH_REG_MIN_START_TORQUE,    2, true,  "min_start_torque" },
    { FEETECH_REG_CW_DEAD_BAND,        1, true,  "cw_dead_band" },
    { FEETECH_REG_CCW_DEAD_BAND,       1, true,  "ccw_dead_band" },
    { FEETECH_REG_OVERLOAD_CURRENT,    2, true,  "overload_current" },
    { FEETECH_REG_RESOLUTION,          1, true,  "resolution" },
    { FEETECH_REG_POSITION_OFFSET,     2, true,  "position_offset" },
    { FEETECH_REG_MODE,                1, true,  "mode" },

    { FEETECH_REG_TORQUE_ENABLE,       1, false, "torque_enable" },
    { FEETECH_REG_ACCELERATION,        1, false, "acceleration" },
    { FEETECH_REG_GOAL_POSITION,       2, false, "goal_position" },
    { FEETECH_REG_GOAL_TIME,           2, false, "goal_time" },
    { FEETECH_REG_GOAL_SPEED,          2, false, "goal_speed" },
    { FEETECH_REG_TORQUE_LIMIT,        2, false, "torque_limit" },
    { FEETECH_REG_LOCK,                1, false, "lock" },
    { FEETECH_REG_PRESENT_POSITION,    2, false, "present_position" },
    { FEETECH_REG_PRESENT_SPEED,       2, false, "present_speed" },
    { FEETECH_REG_PRESENT_LOAD,        2, false, "present_load" },
    { FEETECH_REG_PRESENT_VOLTAGE,     1, false, "present_voltage" },
    { FEETECH_REG_PRESENT_TEMPERATURE, 1, false, "present_temperature" },
    { FEETECH_REG_MOVING,              1, false, "moving" },
    { FEETECH_REG_PRESENT_CURRENT,     2, false, "present_current" },
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

uint8_t feetech_register_width(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL ? info->width : 0;
}

const char *feetech_register_name(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL ? info->name : NULL;
}

bool feetech_register_is_eeprom(uint8_t address)
{
    const register_info_t *info = lookup(address);
    return info != NULL && info->eeprom;
}

uint32_t feetech_position_to_millidegrees(uint16_t position)
{
    if (position > FEETECH_POSITION_MAX) {
        position = FEETECH_POSITION_MAX;
    }

    const uint64_t numerator = (uint64_t)position * FEETECH_RANGE_MILLIDEGREES +
                               FEETECH_POSITION_MAX / 2u;
    return (uint32_t)(numerator / FEETECH_POSITION_MAX);
}

uint16_t feetech_millidegrees_to_position(uint32_t millidegrees)
{
    if (millidegrees >= FEETECH_RANGE_MILLIDEGREES) {
        return FEETECH_POSITION_MAX;
    }

    const uint64_t numerator = (uint64_t)millidegrees * FEETECH_POSITION_MAX +
                               FEETECH_RANGE_MILLIDEGREES / 2u;
    return (uint16_t)(numerator / FEETECH_RANGE_MILLIDEGREES);
}

int16_t feetech_decode_signed_magnitude(uint16_t raw)
{
    /* Masking to 15 bits leaves at most 32767, which is exactly INT16_MAX, so
       neither sign can overflow and no clamp is needed here. */
    const uint16_t magnitude = raw & 0x7FFFu;
    const bool negative = (raw & 0x8000u) != 0;

    return negative ? (int16_t)(-(int32_t)magnitude) : (int16_t)magnitude;
}
