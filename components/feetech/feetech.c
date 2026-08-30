#include "feetech.h"

uint32_t feetech_baud_index_to_rate(uint8_t index)
{
    static const uint32_t rates[] = {
        1000000, 500000, 250000, 128000, 115200, 76800, 57600, 38400,
    };

    return (index < sizeof(rates) / sizeof(rates[0])) ? rates[index] : 0;
}

servo_bus_result_t feetech_bus_init(servo_bus_t *bus, half_duplex_uart_t *uart,
                                    feetech_model_t model)
{
    const servo_bus_config_t config = {
        .uart = uart,
        .endianness = feetech_endianness(model),
        .max_retries = SERVO_BUS_DEFAULT_MAX_RETRIES,
        .response_timeout_us = 0,
    };
    return servo_bus_init(bus, &config);
}

servo_bus_result_t feetech_ping(servo_bus_t *bus, uint8_t id)
{
    return servo_bus_ping(bus, id, NULL);
}

servo_bus_result_t feetech_scan(servo_bus_t *bus, uint8_t *ids, size_t capacity,
                                size_t *found)
{
    if (found != NULL) {
        *found = 0;
    }
    if (bus == NULL || ids == NULL || capacity == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    /* Most IDs are absent, so retrying every empty slot would triple a sweep
       that is already slow. One attempt each; restore the setting after. */
    const uint8_t saved_retries = bus->max_retries;
    bus->max_retries = 0;

    size_t count = 0;
    for (unsigned id = FEETECH_ID_MIN; id <= FEETECH_ID_MAX && count < capacity; id++) {
        if (servo_bus_ping(bus, (uint8_t)id, NULL) == SERVO_BUS_OK) {
            ids[count++] = (uint8_t)id;
        }
    }

    bus->max_retries = saved_retries;

    if (found != NULL) {
        *found = count;
    }
    return SERVO_BUS_OK;
}

servo_bus_result_t feetech_read_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                         uint32_t *value, uint8_t *error_out)
{
    const uint8_t width = feetech_register_width(reg);
    if (width == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_read_value(bus, id, reg, width, value, error_out);
}

servo_bus_result_t feetech_write_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                          uint32_t value, uint8_t *error_out)
{
    const uint8_t width = feetech_register_width(reg);
    if (width == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, reg, value, width, error_out);
}

/* ---------------------------------------------------------------------------
 * Motion
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_set_torque_enable(servo_bus_t *bus, uint8_t id, bool enable)
{
    return servo_bus_write_value(bus, id, FEETECH_REG_TORQUE_ENABLE,
                                 enable ? 1u : 0u, 1, NULL);
}

servo_bus_result_t feetech_set_goal_position(servo_bus_t *bus, uint8_t id,
                                             uint16_t position)
{
    if (position > FEETECH_POSITION_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, FEETECH_REG_GOAL_POSITION, position, 2, NULL);
}

servo_bus_result_t feetech_get_present_position(servo_bus_t *bus, uint8_t id,
                                                uint16_t *position)
{
    if (position == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, FEETECH_REG_PRESENT_POSITION, 2, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *position = (uint16_t)value;
    }
    return result;
}

servo_bus_result_t feetech_set_goal_millidegrees(servo_bus_t *bus, uint8_t id,
                                                 uint32_t millidegrees)
{
    return feetech_set_goal_position(bus, id,
                                     feetech_millidegrees_to_position(millidegrees));
}

servo_bus_result_t feetech_get_present_millidegrees(servo_bus_t *bus, uint8_t id,
                                                    uint32_t *millidegrees)
{
    if (millidegrees == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint16_t position = 0;
    const servo_bus_result_t result = feetech_get_present_position(bus, id, &position);
    if (result == SERVO_BUS_OK) {
        *millidegrees = feetech_position_to_millidegrees(position);
    }
    return result;
}

servo_bus_result_t feetech_set_goal_speed(servo_bus_t *bus, uint8_t id, uint16_t speed)
{
    if (speed > FEETECH_SPEED_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, FEETECH_REG_GOAL_SPEED, speed, 2, NULL);
}

servo_bus_result_t feetech_set_acceleration(servo_bus_t *bus, uint8_t id,
                                            uint8_t acceleration)
{
    return servo_bus_write_value(bus, id, FEETECH_REG_ACCELERATION, acceleration, 1, NULL);
}

servo_bus_result_t feetech_is_moving(servo_bus_t *bus, uint8_t id, bool *moving)
{
    if (moving == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, FEETECH_REG_MOVING, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *moving = (value != 0);
    }
    return result;
}

static servo_bus_result_t read_signed_magnitude(servo_bus_t *bus, uint8_t id,
                                                uint8_t reg, int16_t *out)
{
    if (out == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result = servo_bus_read_value(bus, id, reg, 2, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *out = feetech_decode_signed_magnitude((uint16_t)value);
    }
    return result;
}

servo_bus_result_t feetech_get_present_speed(servo_bus_t *bus, uint8_t id, int16_t *speed)
{
    return read_signed_magnitude(bus, id, FEETECH_REG_PRESENT_SPEED, speed);
}

servo_bus_result_t feetech_get_present_load(servo_bus_t *bus, uint8_t id, int16_t *load)
{
    return read_signed_magnitude(bus, id, FEETECH_REG_PRESENT_LOAD, load);
}

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_get_temperature(servo_bus_t *bus, uint8_t id,
                                           uint8_t *degrees_celsius)
{
    if (degrees_celsius == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, FEETECH_REG_PRESENT_TEMPERATURE, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *degrees_celsius = (uint8_t)value;
    }
    return result;
}

servo_bus_result_t feetech_get_voltage_millivolts(servo_bus_t *bus, uint8_t id,
                                                  uint32_t *millivolts)
{
    if (millivolts == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, FEETECH_REG_PRESENT_VOLTAGE, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *millivolts = feetech_voltage_to_millivolts((uint8_t)value);
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_unlock_eeprom(servo_bus_t *bus, uint8_t id)
{
    return servo_bus_write_value(bus, id, FEETECH_REG_LOCK, FEETECH_LOCK_OPEN, 1, NULL);
}

servo_bus_result_t feetech_lock_eeprom(servo_bus_t *bus, uint8_t id)
{
    return servo_bus_write_value(bus, id, FEETECH_REG_LOCK, FEETECH_LOCK_CLOSED, 1, NULL);
}

servo_bus_result_t feetech_set_id(servo_bus_t *bus, uint8_t id, uint8_t new_id)
{
    if (new_id > FEETECH_ID_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    servo_bus_result_t result = feetech_unlock_eeprom(bus, id);
    if (result != SERVO_BUS_OK) {
        return result;
    }

    result = servo_bus_write_value(bus, id, FEETECH_REG_ID, new_id, 1, NULL);
    if (result != SERVO_BUS_OK) {
        /* The ID did not change, so re-lock using the old one. */
        (void)feetech_lock_eeprom(bus, id);
        return result;
    }

    /* The change is immediate: the servo has already stopped answering to the
       old ID, so the re-lock has to be addressed to the new one. */
    return feetech_lock_eeprom(bus, new_id);
}

servo_bus_result_t feetech_set_mode(servo_bus_t *bus, uint8_t id, uint8_t mode)
{
    servo_bus_result_t result = feetech_unlock_eeprom(bus, id);
    if (result != SERVO_BUS_OK) {
        return result;
    }

    result = servo_bus_write_value(bus, id, FEETECH_REG_MODE, mode, 1, NULL);

    const servo_bus_result_t relock = feetech_lock_eeprom(bus, id);
    return result != SERVO_BUS_OK ? result : relock;
}
