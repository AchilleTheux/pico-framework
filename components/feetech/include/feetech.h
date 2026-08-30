/*
 * feetech - Feetech STS / SMS / SCS smart servos.
 *
 * The same shape as the ax12 component, and for the same reason: both speak
 * Protocol 1.0, so both are a register map and named operations over
 * servo_bus.h. What differs is the control table and the byte order, and the
 * byte order is chosen once by feetech_bus_init().
 *
 *     half_duplex_uart_t uart;
 *     servo_bus_t bus;
 *
 *     half_duplex_uart_init(&uart, &uart_config);
 *     feetech_bus_init(&bus, &uart, FEETECH_MODEL_STS);
 *
 *     feetech_set_torque_enable(&bus, 1, true);
 *     feetech_set_goal_position(&bus, 1, 2048);
 *
 * A Feetech servo leaves the factory with its torque *disabled*, so nothing
 * moves until feetech_set_torque_enable() is called — the single most common
 * reason a new servo appears dead while answering pings perfectly.
 */

#ifndef PICO_FRAMEWORK_FEETECH_H
#define PICO_FRAMEWORK_FEETECH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_bus.h"

#include "feetech_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Feetech servos leave the factory at ID 1 and 1 Mbaud. */
#define FEETECH_DEFAULT_ID 1u
#define FEETECH_DEFAULT_BAUDRATE 1000000u

/* Values for FEETECH_REG_BAUD_RATE. Changing it takes effect immediately. */
#define FEETECH_BAUD_INDEX_1000000 0u
#define FEETECH_BAUD_INDEX_500000  1u
#define FEETECH_BAUD_INDEX_250000  2u
#define FEETECH_BAUD_INDEX_128000  3u
#define FEETECH_BAUD_INDEX_115200  4u
#define FEETECH_BAUD_INDEX_76800   5u
#define FEETECH_BAUD_INDEX_57600   6u
#define FEETECH_BAUD_INDEX_38400   7u

/* Bus rate for a FEETECH_BAUD_INDEX_* value, or 0 if the index is unknown. */
uint32_t feetech_baud_index_to_rate(uint8_t index);

/*
 * Prepare a bus for Feetech servos on an already-initialised UART, choosing
 * the byte order for the model family.
 */
servo_bus_result_t feetech_bus_init(servo_bus_t *bus, half_duplex_uart_t *uart,
                                    feetech_model_t model);

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_ping(servo_bus_t *bus, uint8_t id);

/* See ax12_scan(): a bring-up tool, not something for a control loop. */
servo_bus_result_t feetech_scan(servo_bus_t *bus, uint8_t *ids, size_t capacity,
                                size_t *found);

/* ---------------------------------------------------------------------------
 * Registers
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_read_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                         uint32_t *value, uint8_t *error_out);

servo_bus_result_t feetech_write_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                          uint32_t value, uint8_t *error_out);

/* ---------------------------------------------------------------------------
 * Motion
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_set_torque_enable(servo_bus_t *bus, uint8_t id, bool enable);

servo_bus_result_t feetech_set_goal_position(servo_bus_t *bus, uint8_t id,
                                             uint16_t position);
servo_bus_result_t feetech_get_present_position(servo_bus_t *bus, uint8_t id,
                                                uint16_t *position);

servo_bus_result_t feetech_set_goal_millidegrees(servo_bus_t *bus, uint8_t id,
                                                 uint32_t millidegrees);
servo_bus_result_t feetech_get_present_millidegrees(servo_bus_t *bus, uint8_t id,
                                                    uint32_t *millidegrees);

/* 0 means "as fast as it goes". */
servo_bus_result_t feetech_set_goal_speed(servo_bus_t *bus, uint8_t id, uint16_t speed);

/* 0 to 254; 0 disables the ramp. */
servo_bus_result_t feetech_set_acceleration(servo_bus_t *bus, uint8_t id,
                                            uint8_t acceleration);

servo_bus_result_t feetech_is_moving(servo_bus_t *bus, uint8_t id, bool *moving);

servo_bus_result_t feetech_get_present_speed(servo_bus_t *bus, uint8_t id,
                                             int16_t *speed);
servo_bus_result_t feetech_get_present_load(servo_bus_t *bus, uint8_t id,
                                            int16_t *load);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_get_temperature(servo_bus_t *bus, uint8_t id,
                                           uint8_t *degrees_celsius);
servo_bus_result_t feetech_get_voltage_millivolts(servo_bus_t *bus, uint8_t id,
                                                  uint32_t *millivolts);

/* ---------------------------------------------------------------------------
 * Configuration
 *
 * The EEPROM must be unlocked before an ID or baud-rate change will stick, and
 * both take effect immediately: the servo stops answering on the old setting
 * the moment the write lands.
 * -------------------------------------------------------------------------*/

servo_bus_result_t feetech_unlock_eeprom(servo_bus_t *bus, uint8_t id);
servo_bus_result_t feetech_lock_eeprom(servo_bus_t *bus, uint8_t id);

/* Unlocks, writes the ID, then re-locks using the *new* ID. */
servo_bus_result_t feetech_set_id(servo_bus_t *bus, uint8_t id, uint8_t new_id);

/* Position mode, or continuous rotation under feetech_set_goal_speed(). */
servo_bus_result_t feetech_set_mode(servo_bus_t *bus, uint8_t id, uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FEETECH_H */
