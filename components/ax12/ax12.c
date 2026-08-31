#include "ax12.h"

servo_bus_result_t ax12_bus_init(servo_bus_t *bus, half_duplex_uart_t *uart)
{
    const servo_bus_config_t config = {
        .uart = uart,
        .endianness = SERVO_ENDIAN_LITTLE,
        .max_retries = SERVO_BUS_DEFAULT_MAX_RETRIES,
        .response_timeout_us = 0, /* the component default */
    };
    return servo_bus_init(bus, &config);
}

servo_bus_result_t ax12_ping(servo_bus_t *bus, uint8_t id)
{
    return servo_bus_ping(bus, id, NULL);
}

servo_bus_result_t ax12_scan(servo_bus_t *bus, uint8_t *ids, size_t capacity,
                             size_t *found)
{
    if (found != NULL) {
        *found = 0;
    }
    if (bus == NULL || ids == NULL || capacity == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    /*
     * A scan expects most IDs to be absent, so a timeout per empty slot is the
     * normal case. Retrying each of those would triple the sweep for nothing;
     * drop to a single attempt and put the setting back afterwards.
     */
    const uint8_t saved_retries = bus->max_retries;
    bus->max_retries = 0;

    size_t count = 0;
    for (unsigned id = AX12_ID_MIN; id <= AX12_ID_MAX && count < capacity; id++) {
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

servo_bus_result_t ax12_read_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                      uint32_t *value, uint8_t *error_out)
{
    const uint8_t width = ax12_register_width(reg);
    if (width == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_read_value(bus, id, reg, width, value, error_out);
}

servo_bus_result_t ax12_write_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                       uint32_t value, uint8_t *error_out)
{
    const uint8_t width = ax12_register_width(reg);
    if (width == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, reg, value, width, error_out);
}

/* ---------------------------------------------------------------------------
 * Motion
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_set_torque_enable(servo_bus_t *bus, uint8_t id, bool enable)
{
    return servo_bus_write_value(bus, id, AX12_REG_TORQUE_ENABLE, enable ? 1u : 0u,
                                 1, NULL);
}

servo_bus_result_t ax12_set_goal_position(servo_bus_t *bus, uint8_t id,
                                          uint16_t position)
{
    if (position > AX12_POSITION_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, AX12_REG_GOAL_POSITION, position, 2, NULL);
}

servo_bus_result_t ax12_get_present_position(servo_bus_t *bus, uint8_t id,
                                             uint16_t *position)
{
    if (position == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, AX12_REG_PRESENT_POSITION, 2, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *position = (uint16_t)value;
    }
    return result;
}

servo_bus_result_t ax12_set_goal_millidegrees(servo_bus_t *bus, uint8_t id,
                                              uint32_t millidegrees)
{
    return ax12_set_goal_position(bus, id, ax12_millidegrees_to_position(millidegrees));
}

servo_bus_result_t ax12_get_present_millidegrees(servo_bus_t *bus, uint8_t id,
                                                 uint32_t *millidegrees)
{
    if (millidegrees == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint16_t position = 0;
    const servo_bus_result_t result = ax12_get_present_position(bus, id, &position);
    if (result == SERVO_BUS_OK) {
        *millidegrees = ax12_position_to_millidegrees(position);
    }
    return result;
}

servo_bus_result_t ax12_set_moving_speed(servo_bus_t *bus, uint8_t id, uint16_t speed)
{
    if (speed > AX12_SPEED_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, AX12_REG_MOVING_SPEED, speed, 2, NULL);
}

servo_bus_result_t ax12_set_torque_limit(servo_bus_t *bus, uint8_t id, uint16_t limit)
{
    if (limit > AX12_POSITION_MAX) { /* torque shares the 10-bit range */
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, AX12_REG_TORQUE_LIMIT, limit, 2, NULL);
}

servo_bus_result_t ax12_is_moving(servo_bus_t *bus, uint8_t id, bool *moving)
{
    if (moving == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, AX12_REG_MOVING, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *moving = (value != 0);
    }
    return result;
}

servo_bus_result_t ax12_sync_set_goal_positions(servo_bus_t *bus,
                                               const servo_sync_target_t *targets,
                                               uint8_t count)
{
    if (targets == NULL || count == 0) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    /* Checked before anything goes out: a sync-write is not acknowledged, so a
       servo told to go somewhere impossible would fail silently. */
    for (uint8_t i = 0; i < count; i++) {
        if (targets[i].value > AX12_POSITION_MAX || targets[i].id > AX12_ID_MAX) {
            return SERVO_BUS_ERR_INVALID_ARG;
        }
    }

    return servo_bus_sync_write(bus, AX12_REG_GOAL_POSITION, 2, targets, count);
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
        *out = ax12_decode_signed_magnitude((uint16_t)value);
    }
    return result;
}

servo_bus_result_t ax12_get_present_speed(servo_bus_t *bus, uint8_t id, int16_t *speed)
{
    return read_signed_magnitude(bus, id, AX12_REG_PRESENT_SPEED, speed);
}

servo_bus_result_t ax12_get_present_load(servo_bus_t *bus, uint8_t id, int16_t *load)
{
    return read_signed_magnitude(bus, id, AX12_REG_PRESENT_LOAD, load);
}

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_set_led(servo_bus_t *bus, uint8_t id, bool on)
{
    return servo_bus_write_value(bus, id, AX12_REG_LED, on ? 1u : 0u, 1, NULL);
}

servo_bus_result_t ax12_get_temperature(servo_bus_t *bus, uint8_t id,
                                        uint8_t *degrees_celsius)
{
    if (degrees_celsius == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, AX12_REG_PRESENT_TEMPERATURE, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *degrees_celsius = (uint8_t)value;
    }
    return result;
}

servo_bus_result_t ax12_get_voltage_millivolts(servo_bus_t *bus, uint8_t id,
                                               uint32_t *millivolts)
{
    if (millivolts == NULL) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    uint32_t value = 0;
    const servo_bus_result_t result =
        servo_bus_read_value(bus, id, AX12_REG_PRESENT_VOLTAGE, 1, &value, NULL);
    if (result == SERVO_BUS_OK) {
        *millivolts = ax12_voltage_to_millivolts((uint8_t)value);
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_set_id(servo_bus_t *bus, uint8_t id, uint8_t new_id)
{
    if (new_id > AX12_ID_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }
    return servo_bus_write_value(bus, id, AX12_REG_ID, new_id, 1, NULL);
}

servo_bus_result_t ax12_set_angle_limits(servo_bus_t *bus, uint8_t id,
                                         uint16_t cw_limit, uint16_t ccw_limit)
{
    if (cw_limit > AX12_POSITION_MAX || ccw_limit > AX12_POSITION_MAX) {
        return SERVO_BUS_ERR_INVALID_ARG;
    }

    /* The two limits are adjacent registers, so one write covers both and the
       servo never sees a moment with only half the pair applied. */
    uint8_t data[4];
    servo_protocol_encode_value(&data[0], cw_limit, 2, SERVO_ENDIAN_LITTLE);
    servo_protocol_encode_value(&data[2], ccw_limit, 2, SERVO_ENDIAN_LITTLE);

    return servo_bus_write(bus, id, AX12_REG_CW_ANGLE_LIMIT, data, sizeof(data), NULL);
}
