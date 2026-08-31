#include "i2c_bytes.h"

static bool width_is_supported(uint8_t width)
{
    return width >= 1 && width <= 4;
}

uint32_t i2c_decode_value(const uint8_t *data, uint8_t width,
                          i2c_endianness_t endianness)
{
    if (data == NULL || !width_is_supported(width)) {
        return 0;
    }

    uint32_t value = 0;
    for (uint8_t i = 0; i < width; i++) {
        /* Big-endian: the first byte read is the most significant. */
        const uint8_t byte = (endianness == I2C_ENDIAN_BIG)
            ? data[i]
            : data[width - 1u - i];
        value = (value << 8) | byte;
    }
    return value;
}

void i2c_encode_value(uint8_t *out, uint32_t value, uint8_t width,
                      i2c_endianness_t endianness)
{
    if (out == NULL || !width_is_supported(width)) {
        return;
    }

    for (uint8_t i = 0; i < width; i++) {
        const uint8_t byte = (uint8_t)(value >> (8u * (width - 1u - i)));
        if (endianness == I2C_ENDIAN_BIG) {
            out[i] = byte;
        } else {
            out[width - 1u - i] = byte;
        }
    }
}

int32_t i2c_sign_extend(uint32_t value, uint8_t bits)
{
    if (bits == 0 || bits >= 32) {
        return (int32_t)value;
    }

    const uint32_t mask = (1u << bits) - 1u;
    const uint32_t sign = 1u << (bits - 1u);
    const uint32_t magnitude = value & mask;

    /* The standard trick, written out: subtracting 2^bits from a value whose
       sign bit is set gives the negative it represents, without relying on
       implementation-defined right shifts of signed types. */
    if ((magnitude & sign) != 0) {
        return (int32_t)(magnitude) - (int32_t)(mask + 1u);
    }
    return (int32_t)magnitude;
}
