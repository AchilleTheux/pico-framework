/*
 * i2c_device - a device on an I2C bus, at an address.
 *
 * The Pico SDK's i2c_write_blocking() and i2c_read_blocking() are perfectly
 * usable, so this is not a wrapper around them. What it adds is the thing every
 * device driver otherwise reimplements: reading and writing registers.
 *
 * Nearly every I2C peripheral works the same way — write a register address,
 * then read or write its contents — and nearly every driver for one writes its
 * own version of that, with its own byte order and its own idea of what a
 * timeout means. Here it is written once, with the byte order explicit, so a
 * driver for a new sensor is a register map and some conversions rather than a
 * transport.
 *
 * Every call is bounded by a timeout and reports failure by return value. A
 * device that is not there NACKs its address, which is how i2c_device_present()
 * works and how a scan finds what is on the bus.
 *
 * The caller owns the bus: configure it with i2c_init() and set the pins up
 * before using any of this. Several devices share one bus, so a component that
 * claimed it would be wrong.
 */

#ifndef PICO_FRAMEWORK_I2C_DEVICE_H
#define PICO_FRAMEWORK_I2C_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/i2c.h"

#include "i2c_bytes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enough for a register write of a few bytes plus its address. Raise it for a
   device with long register writes; it only sizes a stack buffer. */
#ifndef I2C_DEVICE_MAX_WRITE
#define I2C_DEVICE_MAX_WRITE 32u
#endif

#ifndef I2C_DEVICE_DEFAULT_TIMEOUT_US
#define I2C_DEVICE_DEFAULT_TIMEOUT_US 10000u
#endif

/* 7-bit addressing. 0x00 and 0x78..0x7F are reserved by the specification. */
#define I2C_ADDRESS_MIN 0x08u
#define I2C_ADDRESS_MAX 0x77u

typedef enum {
    I2C_DEVICE_OK = 0,
    I2C_DEVICE_ERR_INVALID_ARG,
    I2C_DEVICE_ERR_TOO_LONG,     /* more than I2C_DEVICE_MAX_WRITE */
    I2C_DEVICE_ERR_NO_DEVICE,    /* nothing acknowledged the address */
    I2C_DEVICE_ERR_TIMEOUT,
    I2C_DEVICE_ERR_SHORT,        /* fewer bytes moved than asked for */
} i2c_device_result_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t address;
    i2c_endianness_t endianness;
    uint32_t timeout_us;
    bool initialised;
} i2c_device_t;

/*
 * Describe a device. Does no I/O, so it cannot fail for a device that is
 * absent; use i2c_device_present() for that.
 *
 * `endianness` decides how multi-byte register values are read and written.
 * I2C_ENDIAN_BIG, the zero value, is right for nearly every sensor.
 * `timeout_us` of 0 selects I2C_DEVICE_DEFAULT_TIMEOUT_US.
 */
i2c_device_result_t i2c_device_init(i2c_device_t *device, i2c_inst_t *i2c,
                                    uint8_t address, i2c_endianness_t endianness,
                                    uint32_t timeout_us);

/*
 * Does anything answer at this address?
 *
 * **This reads a byte from the device and discards it.** An address-only probe
 * is not possible on this hardware — the I2C block carries the start and stop
 * flags in the same FIFO word as a data item, so the SDK rejects a zero-length
 * transfer — and that discarded byte is a real transaction: it consumes an
 * entry from a read FIFO, advances an auto-incrementing register pointer, and
 * clears a clear-on-read status register.
 *
 * Fine for finding out what is on an idle bus, which is what `scan` in
 * apps/tests/i2c_test uses it for. Not fine against a device that is mid-
 * conversion or holding data to be read; there, read a register known to be
 * harmless for that particular device and check the result instead.
 */
bool i2c_device_present(const i2c_device_t *device);

/* ---------------------------------------------------------------------------
 * Raw transfers, for devices that do not follow the register pattern
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_write(const i2c_device_t *device,
                                     const void *data, size_t len);

i2c_device_result_t i2c_device_read(const i2c_device_t *device,
                                    void *data, size_t len);

/*
 * Write then read without releasing the bus between, which is what a register
 * read requires: a stop condition in the middle would let another master in
 * and lose the address that was just set.
 *
 * `rx_len` of 0 (with `rx` NULL or not) is a plain write: the bus is released
 * normally afterwards, since there is no following read for it to be held
 * open for. `rx` NULL with a non-zero `rx_len` is a caller mistake and returns
 * I2C_DEVICE_ERR_INVALID_ARG without touching the bus.
 */
i2c_device_result_t i2c_device_write_read(const i2c_device_t *device,
                                          const void *tx, size_t tx_len,
                                          void *rx, size_t rx_len);

/* ---------------------------------------------------------------------------
 * Registers with an 8-bit address, which is most devices
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_read_bytes(const i2c_device_t *device, uint8_t reg,
                                          void *data, size_t len);

i2c_device_result_t i2c_device_write_bytes(const i2c_device_t *device, uint8_t reg,
                                           const void *data, size_t len);

/* `width` is 1 to 4 bytes, decoded in the device's byte order. */
i2c_device_result_t i2c_device_read_value(const i2c_device_t *device, uint8_t reg,
                                          uint8_t width, uint32_t *value);

i2c_device_result_t i2c_device_write_value(const i2c_device_t *device, uint8_t reg,
                                           uint8_t width, uint32_t value);

/* ---------------------------------------------------------------------------
 * Registers with a 16-bit address
 *
 * Common on time-of-flight and camera parts. The address itself always goes
 * out high byte first, whatever the device's data byte order.
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_read_bytes16(const i2c_device_t *device, uint16_t reg,
                                            void *data, size_t len);

i2c_device_result_t i2c_device_write_bytes16(const i2c_device_t *device, uint16_t reg,
                                             const void *data, size_t len);

i2c_device_result_t i2c_device_read_value16(const i2c_device_t *device, uint16_t reg,
                                            uint8_t width, uint32_t *value);

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

/*
 * Probe every legal address and record those that answer.
 *
 * A bring-up tool: it takes a timeout per empty address, so a quiet bus costs
 * about 112 timeouts. Worth having — "is the sensor even wired up" is the first
 * question on a new board, and answering it without one of these means guessing.
 */
size_t i2c_bus_scan(i2c_inst_t *i2c, uint8_t *addresses, size_t capacity,
                    uint32_t timeout_us);

const char *i2c_device_result_name(i2c_device_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_I2C_DEVICE_H */
