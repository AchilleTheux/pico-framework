/*
 * Host-side tests for I2C register byte packing.
 *
 * Worth testing for the same reason the servo byte order is: reading a sensor
 * register with the wrong order returns a number in range that looks like a
 * plausible measurement rather than an error.
 *
 * Note the default is the opposite of the servos'. Nearly every I2C sensor
 * sends the high byte first; the servos send the low byte first. Having both in
 * one framework is exactly how one gets used for the other, so both are pinned.
 */

#include "test.h"

#include "i2c_bytes.h"

TEST(the_default_byte_order_is_big_endian)
{
    /* The zero value, so a zero-initialised device configuration is right for
       the common case rather than quietly wrong. */
    CHECK_EQ_INT(I2C_ENDIAN_BIG, 0);
}

TEST(two_byte_values_decode_high_byte_first_by_default)
{
    static const uint8_t raw[2] = { 0x12, 0x34 };

    CHECK_EQ_U32(i2c_decode_value(raw, 2, I2C_ENDIAN_BIG), 0x1234u);
    CHECK_EQ_U32(i2c_decode_value(raw, 2, I2C_ENDIAN_LITTLE), 0x3412u);
}

TEST(the_two_orders_are_actually_different)
{
    /* A test passing under either convention would be worthless. */
    uint8_t big[2];
    uint8_t little[2];

    i2c_encode_value(big, 0xABCD, 2, I2C_ENDIAN_BIG);
    i2c_encode_value(little, 0xABCD, 2, I2C_ENDIAN_LITTLE);

    CHECK_EQ_INT(big[0], 0xAB);
    CHECK_EQ_INT(big[1], 0xCD);
    CHECK_EQ_INT(little[0], 0xCD);
    CHECK_EQ_INT(little[1], 0xAB);
}

TEST(values_round_trip_at_every_supported_width)
{
    static const uint32_t values[] = {
        0, 1, 0xFF, 0x100, 0x1234, 0xFFFF, 0x123456, 0xFFFFFF, 0x12345678, 0xFFFFFFFF,
    };

    for (uint8_t width = 1; width <= 4; width++) {
        const uint32_t mask = (width == 4) ? 0xFFFFFFFFu : ((1u << (8u * width)) - 1u);

        for (unsigned i = 0; i < count_of_(values); i++) {
            const uint32_t value = values[i] & mask;

            uint8_t buffer[4];
            i2c_encode_value(buffer, value, width, I2C_ENDIAN_BIG);
            if (i2c_decode_value(buffer, width, I2C_ENDIAN_BIG) != value) {
                printf("    width %u value 0x%08X did not round-trip (big)\n",
                       width, value);
                CHECK(false);
                return;
            }

            i2c_encode_value(buffer, value, width, I2C_ENDIAN_LITTLE);
            if (i2c_decode_value(buffer, width, I2C_ENDIAN_LITTLE) != value) {
                printf("    width %u value 0x%08X did not round-trip (little)\n",
                       width, value);
                CHECK(false);
                return;
            }
        }
    }
}

TEST(three_byte_values_are_supported)
{
    /* Not an oddity: 24-bit ADCs and pressure sensors are common, and a
       component that only did 1, 2 and 4 would send their drivers back to
       assembling bytes by hand. */
    static const uint8_t raw[3] = { 0x12, 0x34, 0x56 };

    CHECK_EQ_U32(i2c_decode_value(raw, 3, I2C_ENDIAN_BIG), 0x123456u);
    CHECK_EQ_U32(i2c_decode_value(raw, 3, I2C_ENDIAN_LITTLE), 0x563412u);
}

TEST(single_byte_values_are_order_independent)
{
    static const uint8_t raw[1] = { 0xA5 };

    CHECK_EQ_U32(i2c_decode_value(raw, 1, I2C_ENDIAN_BIG), 0xA5u);
    CHECK_EQ_U32(i2c_decode_value(raw, 1, I2C_ENDIAN_LITTLE), 0xA5u);
}

TEST(an_unsupported_width_decodes_to_zero_and_encodes_nothing)
{
    static const uint8_t raw[4] = { 1, 2, 3, 4 };
    CHECK_EQ_U32(i2c_decode_value(raw, 0, I2C_ENDIAN_BIG), 0u);
    CHECK_EQ_U32(i2c_decode_value(raw, 5, I2C_ENDIAN_BIG), 0u);
    CHECK_EQ_U32(i2c_decode_value(NULL, 2, I2C_ENDIAN_BIG), 0u);

    uint8_t buffer[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    i2c_encode_value(buffer, 0x1234, 0, I2C_ENDIAN_BIG);
    i2c_encode_value(buffer, 0x1234, 5, I2C_ENDIAN_BIG);
    CHECK_EQ_INT(buffer[0], 0xAA);   /* untouched */
}

TEST(encoding_writes_only_the_requested_width)
{
    /* A two-byte write must not scribble on the third byte of a caller's
       buffer, which for a register write would put a byte on the wire the
       device never asked for. */
    uint8_t buffer[4] = { 0xAA, 0xAA, 0xAA, 0xAA };

    i2c_encode_value(buffer, 0x1234, 2, I2C_ENDIAN_BIG);
    CHECK_EQ_INT(buffer[0], 0x12);
    CHECK_EQ_INT(buffer[1], 0x34);
    CHECK_EQ_INT(buffer[2], 0xAA);
    CHECK_EQ_INT(buffer[3], 0xAA);
}

/* ---------------------------------------------------------------------------
 * Sign extension
 * -------------------------------------------------------------------------*/

TEST(sign_extension_turns_a_narrow_negative_into_a_negative)
{
    /* The failure it prevents: a 12-bit accelerometer reading of -1 read
       unsigned is 4095, which looks like a large positive acceleration. */
    CHECK_EQ_INT(i2c_sign_extend(0xFFFu, 12), -1);
    CHECK_EQ_INT(i2c_sign_extend(0x800u, 12), -2048);
    CHECK_EQ_INT(i2c_sign_extend(0x7FFu, 12), 2047);
    CHECK_EQ_INT(i2c_sign_extend(0u, 12), 0);
}

TEST(sign_extension_works_at_common_field_widths)
{
    /* 8-bit temperature, 12-bit accelerometer, 16-bit gyroscope, 24-bit ADC. */
    CHECK_EQ_INT(i2c_sign_extend(0xFFu, 8), -1);
    CHECK_EQ_INT(i2c_sign_extend(0x80u, 8), -128);
    CHECK_EQ_INT(i2c_sign_extend(0x7Fu, 8), 127);

    CHECK_EQ_INT(i2c_sign_extend(0xFFFFu, 16), -1);
    CHECK_EQ_INT(i2c_sign_extend(0x8000u, 16), -32768);
    CHECK_EQ_INT(i2c_sign_extend(0x7FFFu, 16), 32767);

    CHECK_EQ_INT(i2c_sign_extend(0xFFFFFFu, 24), -1);
    CHECK_EQ_INT(i2c_sign_extend(0x800000u, 24), -8388608);
}

TEST(sign_extension_ignores_bits_above_the_field)
{
    /* A register may hold flags alongside the value; those must not affect the
       sign or the magnitude. */
    CHECK_EQ_INT(i2c_sign_extend(0xF000u | 0x7FFu, 12), 2047);
    CHECK_EQ_INT(i2c_sign_extend(0xF000u | 0xFFFu, 12), -1);
}

TEST(sign_extension_of_a_full_width_value_is_a_plain_cast)
{
    CHECK_EQ_INT(i2c_sign_extend(0xFFFFFFFFu, 32), -1);
    CHECK_EQ_INT(i2c_sign_extend(5u, 0), 5);
}

TEST(a_decoded_value_can_be_sign_extended)
{
    /* The pair as a driver actually uses them: read two big-endian bytes, then
       interpret them as a signed 16-bit measurement. */
    static const uint8_t raw[2] = { 0xFF, 0x38 };   /* -200 */

    const uint32_t value = i2c_decode_value(raw, 2, I2C_ENDIAN_BIG);
    CHECK_EQ_U32(value, 0xFF38u);
    CHECK_EQ_INT(i2c_sign_extend(value, 16), -200);
}

TEST_MAIN(
    RUN(the_default_byte_order_is_big_endian);
    RUN(two_byte_values_decode_high_byte_first_by_default);
    RUN(the_two_orders_are_actually_different);
    RUN(values_round_trip_at_every_supported_width);
    RUN(three_byte_values_are_supported);
    RUN(single_byte_values_are_order_independent);
    RUN(an_unsupported_width_decodes_to_zero_and_encodes_nothing);
    RUN(encoding_writes_only_the_requested_width);

    RUN(sign_extension_turns_a_narrow_negative_into_a_negative);
    RUN(sign_extension_works_at_common_field_widths);
    RUN(sign_extension_ignores_bits_above_the_field);
    RUN(sign_extension_of_a_full_width_value_is_a_plain_cast);
    RUN(a_decoded_value_can_be_sign_extended);
)
