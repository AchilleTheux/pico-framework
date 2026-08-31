/*
 * i2c_bytes - packing register values for I2C devices.
 *
 * Separate from the transfer code, and free of the Pico SDK, so the byte order
 * can be unit-tested on the host. It is worth testing: an I2C register read
 * with the wrong byte order returns a number in range that looks like a
 * plausible measurement, which is the same trap the servo components have.
 *
 * Note the default. Most I2C devices are big-endian — sensors almost
 * universally send the high byte first — where the servos on the other side of
 * this framework are little-endian. The zero value here is therefore BIG, so
 * that a zero-initialised configuration is right for the common case rather
 * than quietly wrong.
 */

#ifndef PICO_FRAMEWORK_I2C_BYTES_H
#define PICO_FRAMEWORK_I2C_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* High byte first: what nearly every I2C sensor does. */
    I2C_ENDIAN_BIG = 0,
    I2C_ENDIAN_LITTLE = 1,
} i2c_endianness_t;

/* Decode `width` bytes (1, 2, 3 or 4). Returns 0 for an unsupported width.
   Three bytes is not an oddity: 24-bit ADC and pressure sensors are common. */
uint32_t i2c_decode_value(const uint8_t *data, uint8_t width,
                          i2c_endianness_t endianness);

/* Encode a value into `width` bytes. Does nothing for an unsupported width. */
void i2c_encode_value(uint8_t *out, uint32_t value, uint8_t width,
                      i2c_endianness_t endianness);

/*
 * Sign-extend a value of `bits` significant bits.
 *
 * Sensors report signed quantities — temperature, acceleration, rate of turn —
 * in fields narrower than a register, and reading one unsigned turns a small
 * negative into a very large positive. A caller that knows the field width can
 * fix that here rather than reinventing the shift each time.
 */
int32_t i2c_sign_extend(uint32_t value, uint8_t bits);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_I2C_BYTES_H */
