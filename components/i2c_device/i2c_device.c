#include <string.h>

#include "pico/stdlib.h"

#include "i2c_device.h"

static uint32_t timeout_of(const i2c_device_t *device)
{
    return device->timeout_us != 0 ? device->timeout_us
                                   : I2C_DEVICE_DEFAULT_TIMEOUT_US;
}

/*
 * The SDK reports a NACK and a timeout as different negative values, and the
 * difference is worth keeping: a NACK means nothing is at that address, while a
 * timeout means the bus itself is stuck — usually a missing pull-up or a device
 * holding SDA down. Collapsing them into one error is how an afternoon gets
 * spent on the wrong problem.
 */
static i2c_device_result_t translate(int transferred, size_t expected)
{
    if (transferred == PICO_ERROR_TIMEOUT) {
        return I2C_DEVICE_ERR_TIMEOUT;
    }
    if (transferred == PICO_ERROR_GENERIC) {
        return I2C_DEVICE_ERR_NO_DEVICE;
    }
    if (transferred < 0) {
        return I2C_DEVICE_ERR_NO_DEVICE;
    }
    if ((size_t)transferred != expected) {
        return I2C_DEVICE_ERR_SHORT;
    }
    return I2C_DEVICE_OK;
}

i2c_device_result_t i2c_device_init(i2c_device_t *device, i2c_inst_t *i2c,
                                    uint8_t address, i2c_endianness_t endianness,
                                    uint32_t timeout_us)
{
    if (device == NULL || i2c == NULL) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }
    /* Reserved addresses are rejected rather than probed: a general-call or
       10-bit prefix on the wire confuses every other device on the bus. */
    if (address < I2C_ADDRESS_MIN || address > I2C_ADDRESS_MAX) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }

    *device = (i2c_device_t){
        .i2c = i2c,
        .address = address,
        .endianness = endianness,
        .timeout_us = timeout_us,
        .initialised = true,
    };
    return I2C_DEVICE_OK;
}

/* ---------------------------------------------------------------------------
 * Raw transfers
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_write(const i2c_device_t *device,
                                     const void *data, size_t len)
{
    if (device == NULL || !device->initialised || (data == NULL && len > 0)) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return I2C_DEVICE_OK;
    }

    const int moved = i2c_write_timeout_us(device->i2c, device->address,
                                           (const uint8_t *)data, len, false,
                                           timeout_of(device));
    return translate(moved, len);
}

i2c_device_result_t i2c_device_read(const i2c_device_t *device, void *data, size_t len)
{
    if (device == NULL || !device->initialised || (data == NULL && len > 0)) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return I2C_DEVICE_OK;
    }

    const int moved = i2c_read_timeout_us(device->i2c, device->address,
                                          (uint8_t *)data, len, false,
                                          timeout_of(device));
    return translate(moved, len);
}

i2c_device_result_t i2c_device_write_read(const i2c_device_t *device,
                                          const void *tx, size_t tx_len,
                                          void *rx, size_t rx_len)
{
    if (device == NULL || !device->initialised || tx == NULL || tx_len == 0) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }

    /*
     * `nostop` on the write, so the bus is held between the two halves. A stop
     * condition in the middle would release it, and another master — or a
     * repeated start from this one after a delay — could leave the device
     * pointing at a different register than the one just selected.
     */
    const int written = i2c_write_timeout_us(device->i2c, device->address,
                                             (const uint8_t *)tx, tx_len, true,
                                             timeout_of(device));
    const i2c_device_result_t write_result = translate(written, tx_len);
    if (write_result != I2C_DEVICE_OK) {
        return write_result;
    }

    if (rx == NULL || rx_len == 0) {
        return I2C_DEVICE_OK;
    }

    const int read = i2c_read_timeout_us(device->i2c, device->address,
                                         (uint8_t *)rx, rx_len, false,
                                         timeout_of(device));
    return translate(read, rx_len);
}

bool i2c_device_present(const i2c_device_t *device)
{
    if (device == NULL || !device->initialised) {
        return false;
    }

    /*
     * A zero-length read: the address goes out and the device either
     * acknowledges it or does not. Nothing is transferred, so this cannot
     * disturb a device that is mid-conversion.
     */
    uint8_t discard;
    return i2c_read_timeout_us(device->i2c, device->address, &discard, 1, false,
                               timeout_of(device)) >= 0;
}

/* ---------------------------------------------------------------------------
 * Registers
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_read_bytes(const i2c_device_t *device, uint8_t reg,
                                          void *data, size_t len)
{
    return i2c_device_write_read(device, &reg, 1, data, len);
}

i2c_device_result_t i2c_device_write_bytes(const i2c_device_t *device, uint8_t reg,
                                           const void *data, size_t len)
{
    if (device == NULL || (data == NULL && len > 0)) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }
    if (len + 1u > I2C_DEVICE_MAX_WRITE) {
        return I2C_DEVICE_ERR_TOO_LONG;
    }

    /* Address and data in one transfer: the device expects them without a stop
       between, and splitting them would select the register and then abandon
       it. */
    uint8_t buffer[I2C_DEVICE_MAX_WRITE];
    buffer[0] = reg;
    if (len > 0) {
        memcpy(&buffer[1], data, len);
    }

    return i2c_device_write(device, buffer, len + 1u);
}

i2c_device_result_t i2c_device_read_value(const i2c_device_t *device, uint8_t reg,
                                          uint8_t width, uint32_t *value)
{
    if (device == NULL || value == NULL || width < 1 || width > 4) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }

    uint8_t raw[4];
    const i2c_device_result_t result = i2c_device_read_bytes(device, reg, raw, width);
    if (result != I2C_DEVICE_OK) {
        return result;
    }

    *value = i2c_decode_value(raw, width, device->endianness);
    return I2C_DEVICE_OK;
}

i2c_device_result_t i2c_device_write_value(const i2c_device_t *device, uint8_t reg,
                                           uint8_t width, uint32_t value)
{
    if (device == NULL || width < 1 || width > 4) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }

    uint8_t raw[4];
    i2c_encode_value(raw, value, width, device->endianness);
    return i2c_device_write_bytes(device, reg, raw, width);
}

/* ---------------------------------------------------------------------------
 * 16-bit register addresses
 * -------------------------------------------------------------------------*/

i2c_device_result_t i2c_device_read_bytes16(const i2c_device_t *device, uint16_t reg,
                                            void *data, size_t len)
{
    /* The address is always high byte first, whatever the device's data byte
       order — that is a property of the register pointer, not of the data. */
    const uint8_t address[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return i2c_device_write_read(device, address, sizeof(address), data, len);
}

i2c_device_result_t i2c_device_write_bytes16(const i2c_device_t *device, uint16_t reg,
                                             const void *data, size_t len)
{
    if (device == NULL || (data == NULL && len > 0)) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }
    if (len + 2u > I2C_DEVICE_MAX_WRITE) {
        return I2C_DEVICE_ERR_TOO_LONG;
    }

    uint8_t buffer[I2C_DEVICE_MAX_WRITE];
    buffer[0] = (uint8_t)(reg >> 8);
    buffer[1] = (uint8_t)reg;
    if (len > 0) {
        memcpy(&buffer[2], data, len);
    }

    return i2c_device_write(device, buffer, len + 2u);
}

i2c_device_result_t i2c_device_read_value16(const i2c_device_t *device, uint16_t reg,
                                            uint8_t width, uint32_t *value)
{
    if (device == NULL || value == NULL || width < 1 || width > 4) {
        return I2C_DEVICE_ERR_INVALID_ARG;
    }

    uint8_t raw[4];
    const i2c_device_result_t result = i2c_device_read_bytes16(device, reg, raw, width);
    if (result != I2C_DEVICE_OK) {
        return result;
    }

    *value = i2c_decode_value(raw, width, device->endianness);
    return I2C_DEVICE_OK;
}

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

size_t i2c_bus_scan(i2c_inst_t *i2c, uint8_t *addresses, size_t capacity,
                    uint32_t timeout_us)
{
    if (i2c == NULL || addresses == NULL || capacity == 0) {
        return 0;
    }

    size_t found = 0;
    for (uint8_t address = I2C_ADDRESS_MIN;
         address <= I2C_ADDRESS_MAX && found < capacity; address++) {
        uint8_t discard;
        if (i2c_read_timeout_us(i2c, address, &discard, 1, false,
                                timeout_us != 0 ? timeout_us
                                                : I2C_DEVICE_DEFAULT_TIMEOUT_US) >= 0) {
            addresses[found++] = address;
        }
    }
    return found;
}

const char *i2c_device_result_name(i2c_device_result_t result)
{
    switch (result) {
        case I2C_DEVICE_OK:              return "ok";
        case I2C_DEVICE_ERR_INVALID_ARG: return "invalid argument";
        case I2C_DEVICE_ERR_TOO_LONG:    return "write longer than the buffer";
        case I2C_DEVICE_ERR_NO_DEVICE:   return "no device acknowledged";
        case I2C_DEVICE_ERR_TIMEOUT:     return "bus timeout";
        case I2C_DEVICE_ERR_SHORT:       return "fewer bytes than asked for";
        default:                         return "unknown";
    }
}
