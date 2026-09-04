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

/* Send every servo to its own position in one packet; see
   servo_bus_sync_write(). */
servo_bus_result_t feetech_sync_set_goal_positions(servo_bus_t *bus,
                                                  const servo_sync_target_t *targets,
                                                  uint8_t count);

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

/*
 * Move a servo, and the host with it, to `rate`.
 *
 * Both ends have to move or the servo is unreachable, so this call does both:
 * it unlocks the EEPROM, writes FEETECH_REG_BAUD_RATE, re-clocks the bus, and
 * pings to confirm the servo answers at the new rate before locking the EEPROM
 * again. `rate` must be one of the eight the table offers; see
 * feetech_rate_to_baud_index().
 *
 * Nothing is written when `rate` is not reachable at both ends. A servo that
 * fails to answer at the new rate leaves the bus back at the rate it was
 * running and the EEPROM locked again, so neither a refusal nor a failure
 * strands a servo or leaves its EEPROM open.
 *
 * Servos *other* than `id` are left at the old rate and so are left behind.
 * SERVO_PROTOCOL_BROADCAST_ID moves the whole bus at once, but nothing
 * acknowledges a broadcast: that form cannot confirm anything and cannot
 * re-lock the EEPROM either, so follow it with feetech_scan() and
 * feetech_lock_eeprom() per servo.
 *
 * The servo's acknowledgement is sent as the write takes effect, and whether
 * it arrives at the old rate, at the new one, or half in each is not something
 * to rely on. A reply that is missing or corrupted is therefore not treated as
 * a failure — only a packet that could not be sent at all is; the ping
 * afterwards is what decides.
 */
servo_bus_result_t feetech_set_baudrate(servo_bus_t *bus, uint8_t id, uint32_t rate);

/*
 * How long a servo is given to come back after a factory reset. No datasheet
 * figure, so this is a margin: the servo is polled until it answers.
 */
#ifndef FEETECH_RESET_TIMEOUT_MS
#define FEETECH_RESET_TIMEOUT_MS 1000u
#endif

/*
 * Put a servo back to its factory settings and follow it there.
 *
 * The rescue for a servo whose configuration is unknown or wrong: it restores
 * the whole EEPROM, so the servo comes back at FEETECH_DEFAULT_ID and
 * FEETECH_DEFAULT_BAUDRATE. The host follows to the factory rate, polls until
 * the servo answers on the factory ID, and only then reports success.
 *
 * `id` addresses the servo as it is *now*. Nothing is sent when the host cannot
 * be clocked at the factory rate, and if the servo does not come back the bus
 * is returned to the rate it was running and the EEPROM is locked again.
 *
 * One servo at a time, and SERVO_PROTOCOL_BROADCAST_ID is refused: the reset
 * takes the ID with it, so resetting several servos on one bus leaves them all
 * answering as ID 1.
 *
 * The EEPROM is unlocked first. Whether a Feetech servo needs that before it
 * will honour a reset is not documented either way, and unlocking costs one
 * write to a RAM register, so this does it rather than leaving the question to
 * whoever is holding the servo.
 *
 * This is not the way out of an unknown *baud rate* — the servo has to hear
 * the instruction for it to do anything, so feetech_scan() across
 * feetech_baud_rate_table() comes first.
 */
servo_bus_result_t feetech_factory_reset(servo_bus_t *bus, uint8_t id);

/* Position mode, or continuous rotation under feetech_set_goal_speed(). */
servo_bus_result_t feetech_set_mode(servo_bus_t *bus, uint8_t id, uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FEETECH_H */
