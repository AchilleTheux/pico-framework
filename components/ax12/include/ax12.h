/*
 * ax12 - Dynamixel AX-12 / AX-18 servos.
 *
 * A register map (ax12_registers.h) and named operations over servo_bus.h,
 * which supplies the packet format, retries and transport. There is no ax12_t:
 * a servo has no state on this side of the wire, so every call takes the bus
 * and an ID.
 *
 *     half_duplex_uart_t uart;
 *     servo_bus_t bus;
 *
 *     half_duplex_uart_init(&uart, &uart_config);
 *     ax12_bus_init(&bus, &uart);              // sets the right byte order
 *
 *     ax12_set_torque_enable(&bus, 1, true);
 *     ax12_set_goal_position(&bus, 1, 512);
 *
 * Every call returns a servo_bus_result_t. A servo that answers but reports a
 * fault of its own still returns SERVO_BUS_OK; the fault reaches the caller
 * through the optional `error_out`.
 */

#ifndef PICO_FRAMEWORK_AX12_H
#define PICO_FRAMEWORK_AX12_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "servo_bus.h"

#include "ax12_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AX-12 servos leave the factory at ID 1 and 1 Mbaud. */
#define AX12_DEFAULT_ID 1u
#define AX12_DEFAULT_BAUDRATE 1000000u

/*
 * Prepare a bus for AX-12 servos on an already-initialised UART.
 *
 * Exists so the byte order is chosen once, correctly: AX-12 registers are
 * little-endian, and getting that wrong turns position 512 into position 2
 * rather than into an error.
 */
servo_bus_result_t ax12_bus_init(servo_bus_t *bus, half_duplex_uart_t *uart);

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_ping(servo_bus_t *bus, uint8_t id);

/*
 * Ping every ID from AX12_ID_MIN to AX12_ID_MAX and record those that answer.
 *
 * Takes roughly `254 * (packet time + response timeout)` — about a second at
 * default settings, so it is a bring-up tool rather than something for a
 * control loop. `found` is always written.
 */
servo_bus_result_t ax12_scan(servo_bus_t *bus, uint8_t *ids, size_t capacity,
                             size_t *found);

/* ---------------------------------------------------------------------------
 * Registers
 *
 * The width comes from the control table, so a caller names a register rather
 * than remembering whether it is one byte or two.
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_read_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                      uint32_t *value, uint8_t *error_out);

servo_bus_result_t ax12_write_register(servo_bus_t *bus, uint8_t id, uint8_t reg,
                                       uint32_t value, uint8_t *error_out);

/* ---------------------------------------------------------------------------
 * Motion
 * -------------------------------------------------------------------------*/

/* Torque is off when a servo powers up; nothing moves until this is set. */
servo_bus_result_t ax12_set_torque_enable(servo_bus_t *bus, uint8_t id, bool enable);

/* 0 to 1023 across the servo's 300 degrees. Values above 1023 are rejected
   rather than wrapped, since a wrapped goal is a sudden unwanted movement. */
servo_bus_result_t ax12_set_goal_position(servo_bus_t *bus, uint8_t id,
                                          uint16_t position);
servo_bus_result_t ax12_get_present_position(servo_bus_t *bus, uint8_t id,
                                             uint16_t *position);

/* Angle form of the same pair, in millidegrees; see ax12_registers.h. */
servo_bus_result_t ax12_set_goal_millidegrees(servo_bus_t *bus, uint8_t id,
                                              uint32_t millidegrees);
servo_bus_result_t ax12_get_present_millidegrees(servo_bus_t *bus, uint8_t id,
                                                 uint32_t *millidegrees);

/* 0 means "as fast as it goes", not "stop". 1 to 1023 is about 0.111 rpm each. */
servo_bus_result_t ax12_set_moving_speed(servo_bus_t *bus, uint8_t id, uint16_t speed);

/* Fraction of maximum torque, 0 to 1023. */
servo_bus_result_t ax12_set_torque_limit(servo_bus_t *bus, uint8_t id, uint16_t limit);

servo_bus_result_t ax12_is_moving(servo_bus_t *bus, uint8_t id, bool *moving);

/*
 * Send every servo to its own position in one packet, so they all start
 * together. See servo_bus_sync_write() for what that buys and what it costs.
 * Positions above AX12_POSITION_MAX are rejected before anything is sent.
 */
servo_bus_result_t ax12_sync_set_goal_positions(servo_bus_t *bus,
                                               const servo_sync_target_t *targets,
                                               uint8_t count);

/* Signed: negative is clockwise. See ax12_decode_signed_magnitude(). */
servo_bus_result_t ax12_get_present_speed(servo_bus_t *bus, uint8_t id, int16_t *speed);
servo_bus_result_t ax12_get_present_load(servo_bus_t *bus, uint8_t id, int16_t *load);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_set_led(servo_bus_t *bus, uint8_t id, bool on);
servo_bus_result_t ax12_get_temperature(servo_bus_t *bus, uint8_t id,
                                        uint8_t *degrees_celsius);
servo_bus_result_t ax12_get_voltage_millivolts(servo_bus_t *bus, uint8_t id,
                                               uint32_t *millivolts);

/* ---------------------------------------------------------------------------
 * Configuration
 *
 * These write EEPROM, which has limited endurance and takes effect at once —
 * changing an ID means the servo stops answering to the old one immediately.
 * -------------------------------------------------------------------------*/

servo_bus_result_t ax12_set_id(servo_bus_t *bus, uint8_t id, uint8_t new_id);

/*
 * Move a servo, and the host with it, to `rate`.
 *
 * Both ends have to move or the servo is unreachable, so this call does both:
 * it writes AX12_REG_BAUD_RATE, re-clocks the bus, and pings to confirm the
 * servo is answering at the new rate. `rate` may be a datasheet name — 115200
 * is accepted and both ends end up at the 117647 baud that name really means.
 * See ax12_rate_to_baud_value() for what is reachable.
 *
 * Nothing is written when `rate` is not reachable at both ends, so a refusal
 * costs neither an EEPROM write nor a servo that has vanished.
 *
 * Servos *other* than `id` are left at the old rate and so are left behind.
 * To move a whole bus at once, pass SERVO_PROTOCOL_BROADCAST_ID: every servo
 * takes the change and the host follows. Nothing acknowledges a broadcast, so
 * that form cannot confirm anything — follow it with ax12_scan().
 *
 * The servo's acknowledgement is sent as the write takes effect, and whether
 * it arrives at the old rate, at the new one, or half in each is not something
 * to rely on. A reply that is missing or corrupted is therefore not treated as
 * a failure — only a packet that could not be sent at all is; the ping
 * afterwards is what decides.
 */
servo_bus_result_t ax12_set_baudrate(servo_bus_t *bus, uint8_t id, uint32_t rate);

/*
 * How long a servo is given to come back after a factory reset.
 *
 * The datasheet does not specify a reboot time, so this is a margin rather
 * than a figure: the servo is polled until it answers or this expires.
 */
#ifndef AX12_RESET_TIMEOUT_MS
#define AX12_RESET_TIMEOUT_MS 1000u
#endif

/*
 * Put a servo back to its factory settings and follow it there.
 *
 * The rescue for a servo whose configuration is unknown or wrong: it restores
 * the whole EEPROM, so the servo comes back at AX12_DEFAULT_ID and
 * AX12_DEFAULT_BAUDRATE with its angle limits, compliance and alarms as they
 * left the factory. The host follows to the factory rate, polls until the
 * servo answers on the factory ID, and only then reports success — so a
 * SERVO_BUS_OK here means the servo is reachable at ID 1, not merely that a
 * packet went out.
 *
 * `id` addresses the servo as it is *now*. Nothing is sent when the host
 * cannot be clocked at the factory rate, and if the servo does not come back
 * the bus is returned to the rate it was running.
 *
 * One servo at a time, and SERVO_PROTOCOL_BROADCAST_ID is refused: the reset
 * takes the ID with it, so resetting several servos on one bus leaves them all
 * answering as ID 1. Give each one its own ID again, with ax12_set_id(), before
 * resetting the next.
 *
 * This is not the way out of an unknown *baud rate* — the servo has to hear the
 * instruction for it to do anything, so ax12_scan() across
 * ax12_baud_rate_table() comes first.
 */
servo_bus_result_t ax12_factory_reset(servo_bus_t *bus, uint8_t id);

/*
 * Both limits zero puts the servo in continuous-rotation mode, where
 * ax12_set_moving_speed() controls direction and speed and goal position is
 * ignored.
 */
servo_bus_result_t ax12_set_angle_limits(servo_bus_t *bus, uint8_t id,
                                         uint16_t cw_limit, uint16_t ccw_limit);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_AX12_H */
