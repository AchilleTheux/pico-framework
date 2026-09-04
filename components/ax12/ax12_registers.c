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

uint32_t ax12_baud_value_to_rate(uint8_t value)
{
    if (value < AX12_BAUD_VALUE_MIN) {
        return 0;
    }
    /* AX12_BAUD_VALUE_MAX is 254 and value is a uint8_t, so only 255 is out of
       range at the top; the divisor cannot overflow. */
    if (value > AX12_BAUD_VALUE_MAX) {
        return 0;
    }
    return AX12_BAUD_CLOCK / ((uint32_t)value + 1u);
}

bool ax12_rate_to_baud_value(uint32_t rate, uint8_t *value)
{
    if (value == NULL || rate == 0) {
        return false;
    }

    /* Nearest divisor to 2000000/rate, then step back to the register value. */
    const uint32_t divisor = (AX12_BAUD_CLOCK + rate / 2u) / rate;
    if (divisor < AX12_BAUD_VALUE_MIN + 1u || divisor > AX12_BAUD_VALUE_MAX + 1u) {
        return false;
    }

    const uint8_t candidate = (uint8_t)(divisor - 1u);
    const uint32_t actual = ax12_baud_value_to_rate(candidate);

    /*
     * Rounding the divisor always gives the closest reachable rate, but at the
     * top of the range the steps are huge — 1 Mbaud, 667 k, 500 k — so
     * "closest" can still be nowhere near what was asked. Reject that rather
     * than silently setting a servo to a rate the caller did not mean.
     */
    const uint32_t difference = actual > rate ? actual - rate : rate - actual;
    if ((uint64_t)difference * 100u > (uint64_t)rate * 3u) {
        return false;
    }

    *value = candidate;
    return true;
}

const uint32_t *ax12_baud_rate_table(size_t *count)
{
    /* Divisors 1, 3, 4, 7, 9, 16, 34, 103 and 207, as the exact rates they
       produce. The datasheet calls the last four 115200, 57600, 19200 and
       9600. */
    static const uint32_t rates[] = {
        1000000, 500000, 400000, 250000, 200000, 117647, 57142, 19230, 9615,
    };

    if (count != NULL) {
        *count = sizeof(rates) / sizeof(rates[0]);
    }
    return rates;
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
