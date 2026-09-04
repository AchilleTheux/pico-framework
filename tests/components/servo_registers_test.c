/*
 * Host-side tests for the AX-12 and Feetech control tables.
 *
 * The width table is the thing worth testing here. Reading a two-byte register
 * as one byte returns its low half, which is a number in range that looks
 * entirely plausible — so a wrong width is a silent wrong answer, not a
 * failure. The same goes for byte order.
 */

#include <string.h>

#include "test.h"

#include "ax12_registers.h"
#include "feetech_registers.h"

/* ---------------------------------------------------------------------------
 * AX-12 register table
 * -------------------------------------------------------------------------*/

TEST(ax12_two_byte_registers_are_two_bytes)
{
    /* Every register the AX-12 datasheet lists as a word. Getting one of these
       wrong halves a position or speed reading. */
    static const uint8_t words[] = {
        AX12_REG_MODEL_NUMBER, AX12_REG_CW_ANGLE_LIMIT, AX12_REG_CCW_ANGLE_LIMIT,
        AX12_REG_MAX_TORQUE, AX12_REG_DOWN_CALIBRATION, AX12_REG_UP_CALIBRATION,
        AX12_REG_GOAL_POSITION, AX12_REG_MOVING_SPEED, AX12_REG_TORQUE_LIMIT,
        AX12_REG_PRESENT_POSITION, AX12_REG_PRESENT_SPEED, AX12_REG_PRESENT_LOAD,
        AX12_REG_PUNCH,
    };

    for (unsigned i = 0; i < count_of_(words); i++) {
        if (ax12_register_width(words[i]) != 2) {
            printf("    register 0x%02X (%s)\n", words[i],
                   ax12_register_name(words[i]));
            CHECK_EQ_INT(ax12_register_width(words[i]), 2);
        }
    }
}

TEST(ax12_single_byte_registers_are_one_byte)
{
    static const uint8_t bytes[] = {
        AX12_REG_FIRMWARE_VERSION, AX12_REG_ID, AX12_REG_BAUD_RATE,
        AX12_REG_RETURN_DELAY_TIME, AX12_REG_TEMPERATURE_LIMIT,
        AX12_REG_MIN_VOLTAGE_LIMIT, AX12_REG_MAX_VOLTAGE_LIMIT,
        AX12_REG_STATUS_RETURN_LEVEL, AX12_REG_ALARM_LED, AX12_REG_SHUTDOWN,
        AX12_REG_TORQUE_ENABLE, AX12_REG_LED, AX12_REG_CW_COMPLIANCE_MARGIN,
        AX12_REG_CCW_COMPLIANCE_MARGIN, AX12_REG_CW_COMPLIANCE_SLOPE,
        AX12_REG_CCW_COMPLIANCE_SLOPE, AX12_REG_PRESENT_VOLTAGE,
        AX12_REG_PRESENT_TEMPERATURE, AX12_REG_REGISTERED, AX12_REG_MOVING,
        AX12_REG_LOCK,
    };

    for (unsigned i = 0; i < count_of_(bytes); i++) {
        if (ax12_register_width(bytes[i]) != 1) {
            printf("    register 0x%02X (%s)\n", bytes[i],
                   ax12_register_name(bytes[i]));
            CHECK_EQ_INT(ax12_register_width(bytes[i]), 1);
        }
    }
}

TEST(the_high_half_of_an_ax12_word_register_is_not_a_register)
{
    /* Addressing 0x1F instead of 0x1E is a plausible typo, and reading one
       byte from it would return half a position. It must report width 0. */
    CHECK_EQ_INT(ax12_register_width(AX12_REG_GOAL_POSITION + 1), 0);
    CHECK_EQ_INT(ax12_register_width(AX12_REG_PRESENT_POSITION + 1), 0);
    CHECK_EQ_INT(ax12_register_width(AX12_REG_MOVING_SPEED + 1), 0);
}

TEST(an_unknown_ax12_address_has_no_width_or_name)
{
    CHECK_EQ_INT(ax12_register_width(0xFE), 0);
    CHECK(ax12_register_name(0xFE) == NULL);
    CHECK(!ax12_register_is_eeprom(0xFE));
}

TEST(the_ax12_eeprom_boundary_is_where_the_datasheet_says)
{
    /* Everything below 0x18 is EEPROM and wears out; everything from 0x18 is
       RAM and resets on power-up. */
    CHECK(ax12_register_is_eeprom(AX12_REG_ID));
    CHECK(ax12_register_is_eeprom(AX12_REG_CCW_ANGLE_LIMIT));
    CHECK(ax12_register_is_eeprom(AX12_REG_SHUTDOWN));
    CHECK(!ax12_register_is_eeprom(AX12_REG_TORQUE_ENABLE));
    CHECK(!ax12_register_is_eeprom(AX12_REG_GOAL_POSITION));
    CHECK(!ax12_register_is_eeprom(AX12_REG_PRESENT_POSITION));
}

TEST(every_named_ax12_register_has_a_width)
{
    /* Guards against a table entry gaining a name but not a width. */
    for (unsigned address = 0; address < 256; address++) {
        const char *name = ax12_register_name((uint8_t)address);
        if (name != NULL && ax12_register_width((uint8_t)address) == 0) {
            printf("    0x%02X named '%s' has width 0\n", address, name);
            CHECK(false);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * AX-12 conversions
 * -------------------------------------------------------------------------*/

TEST(ax12_position_endpoints_map_to_the_ends_of_the_arc)
{
    CHECK_EQ_U32(ax12_position_to_millidegrees(0), 0);
    CHECK_EQ_U32(ax12_position_to_millidegrees(AX12_POSITION_MAX),
                 AX12_RANGE_MILLIDEGREES);
}

TEST(ax12_position_centre_is_the_middle_of_the_arc)
{
    /* 512 of 1023 is a hair past halfway: 150220 rather than 150000. */
    const uint32_t centre = ax12_position_to_millidegrees(512);
    CHECK(centre > 149000 && centre < 151000);
}

TEST(ax12_position_survives_a_round_trip)
{
    /* Converting to an angle and back must return the same count for every
       position, or a goal set in degrees would drift. */
    for (unsigned position = 0; position <= AX12_POSITION_MAX; position++) {
        const uint32_t md = ax12_position_to_millidegrees((uint16_t)position);
        const uint16_t back = ax12_millidegrees_to_position(md);
        if (back != position) {
            printf("    position %u became %u via %u millidegrees\n",
                   position, back, md);
            CHECK(false);
            return;
        }
    }
}

TEST(ax12_angles_beyond_the_arc_clamp_rather_than_wrap)
{
    /* A wrapped goal is a sudden unwanted movement; a clamped one is not. */
    CHECK_EQ_INT(ax12_millidegrees_to_position(AX12_RANGE_MILLIDEGREES),
                 AX12_POSITION_MAX);
    CHECK_EQ_INT(ax12_millidegrees_to_position(AX12_RANGE_MILLIDEGREES * 10),
                 AX12_POSITION_MAX);
    CHECK_EQ_INT(ax12_millidegrees_to_position(0xFFFFFFFFu), AX12_POSITION_MAX);
}

TEST(ax12_positions_above_the_range_clamp)
{
    CHECK_EQ_U32(ax12_position_to_millidegrees(5000), AX12_RANGE_MILLIDEGREES);
}

TEST(ax12_signed_magnitude_uses_bit_ten_for_direction)
{
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0), 0);
    CHECK_EQ_INT(ax12_decode_signed_magnitude(300), 300);
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0x0400), 0);      /* -0 is 0 */
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0x0400 | 300), -300);
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0x03FF), 1023);
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0x07FF), -1023);
}

TEST(ax12_signed_magnitude_ignores_bits_above_the_field)
{
    /* Bits 11..15 are undefined in the AX-12 encoding; they must not leak
       into the magnitude. */
    CHECK_EQ_INT(ax12_decode_signed_magnitude(0xF800 | 100), 100);
}

TEST(ax12_voltage_is_tenths_of_a_volt)
{
    CHECK_EQ_U32(ax12_voltage_to_millivolts(120), 12000); /* 12.0 V */
    CHECK_EQ_U32(ax12_voltage_to_millivolts(0), 0);
}

/* ---------------------------------------------------------------------------
 * Feetech register table
 * -------------------------------------------------------------------------*/

TEST(feetech_two_byte_registers_are_two_bytes)
{
    static const uint8_t words[] = {
        FEETECH_REG_MODEL, FEETECH_REG_MIN_ANGLE_LIMIT, FEETECH_REG_MAX_ANGLE_LIMIT,
        FEETECH_REG_MAX_TORQUE, FEETECH_REG_MIN_START_TORQUE,
        FEETECH_REG_OVERLOAD_CURRENT, FEETECH_REG_POSITION_OFFSET,
        FEETECH_REG_GOAL_POSITION, FEETECH_REG_GOAL_TIME, FEETECH_REG_GOAL_SPEED,
        FEETECH_REG_TORQUE_LIMIT, FEETECH_REG_PRESENT_POSITION,
        FEETECH_REG_PRESENT_SPEED, FEETECH_REG_PRESENT_LOAD,
        FEETECH_REG_PRESENT_CURRENT,
    };

    for (unsigned i = 0; i < count_of_(words); i++) {
        if (feetech_register_width(words[i]) != 2) {
            printf("    register 0x%02X (%s)\n", words[i],
                   feetech_register_name(words[i]));
            CHECK_EQ_INT(feetech_register_width(words[i]), 2);
        }
    }
}

TEST(feetech_single_byte_registers_are_one_byte)
{
    static const uint8_t bytes[] = {
        FEETECH_REG_ID, FEETECH_REG_BAUD_RATE, FEETECH_REG_RETURN_DELAY,
        FEETECH_REG_STATUS_RETURN, FEETECH_REG_MAX_TEMPERATURE,
        FEETECH_REG_MAX_VOLTAGE, FEETECH_REG_MIN_VOLTAGE, FEETECH_REG_SETTING_BYTE,
        FEETECH_REG_PROTECTION, FEETECH_REG_ALARM_LED, FEETECH_REG_CW_DEAD_BAND,
        FEETECH_REG_CCW_DEAD_BAND, FEETECH_REG_RESOLUTION, FEETECH_REG_MODE,
        FEETECH_REG_TORQUE_ENABLE, FEETECH_REG_ACCELERATION, FEETECH_REG_LOCK,
        FEETECH_REG_PRESENT_VOLTAGE, FEETECH_REG_PRESENT_TEMPERATURE,
        FEETECH_REG_MOVING,
    };

    for (unsigned i = 0; i < count_of_(bytes); i++) {
        if (feetech_register_width(bytes[i]) != 1) {
            printf("    register 0x%02X (%s)\n", bytes[i],
                   feetech_register_name(bytes[i]));
            CHECK_EQ_INT(feetech_register_width(bytes[i]), 1);
        }
    }
}

TEST(feetech_and_ax12_control_tables_really_are_different)
{
    /*
     * The protocol is shared, the register maps are not. If these ever
     * coincided it would mean one table had been copied from the other, and
     * writing a goal position to the wrong address is a silent misconfiguration.
     */
    CHECK(FEETECH_REG_GOAL_POSITION != AX12_REG_GOAL_POSITION);
    CHECK(FEETECH_REG_PRESENT_POSITION != AX12_REG_PRESENT_POSITION);
    CHECK(FEETECH_REG_TORQUE_ENABLE != AX12_REG_TORQUE_ENABLE);
    CHECK(FEETECH_REG_ID != AX12_REG_ID);
}

TEST(every_named_feetech_register_has_a_width)
{
    for (unsigned address = 0; address < 256; address++) {
        const char *name = feetech_register_name((uint8_t)address);
        if (name != NULL && feetech_register_width((uint8_t)address) == 0) {
            printf("    0x%02X named '%s' has width 0\n", address, name);
            CHECK(false);
            return;
        }
    }
}

TEST(the_feetech_eeprom_boundary_is_where_the_datasheet_says)
{
    CHECK(feetech_register_is_eeprom(FEETECH_REG_ID));
    CHECK(feetech_register_is_eeprom(FEETECH_REG_MODE));
    CHECK(!feetech_register_is_eeprom(FEETECH_REG_TORQUE_ENABLE));
    CHECK(!feetech_register_is_eeprom(FEETECH_REG_GOAL_POSITION));

    /* LOCK is in RAM even though it gates EEPROM writes, which is easy to get
       backwards. */
    CHECK(!feetech_register_is_eeprom(FEETECH_REG_LOCK));
}

/* ---------------------------------------------------------------------------
 * Feetech model differences
 * -------------------------------------------------------------------------*/

TEST(the_two_feetech_families_disagree_about_byte_order)
{
    /* The single most consequential difference between the families, and the
       reason feetech_bus_init() takes a model. */
    CHECK_EQ_INT(feetech_endianness(FEETECH_MODEL_STS), SERVO_ENDIAN_LITTLE);
    CHECK_EQ_INT(feetech_endianness(FEETECH_MODEL_SCS), SERVO_ENDIAN_BIG);
}

TEST(reading_a_position_with_the_wrong_byte_order_is_silently_wrong)
{
    /*
     * Demonstrates why the model is not optional. Position 2048 encoded for an
     * STS and decoded as an SCS gives 8, which is in range and looks like a
     * servo sitting near one end rather than like a fault.
     */
    uint8_t encoded[2];
    servo_protocol_encode_value(encoded, 2048, 2, feetech_endianness(FEETECH_MODEL_STS));

    const uint32_t correct =
        servo_protocol_decode_value(encoded, 2, feetech_endianness(FEETECH_MODEL_STS));
    const uint32_t wrong =
        servo_protocol_decode_value(encoded, 2, feetech_endianness(FEETECH_MODEL_SCS));

    CHECK_EQ_U32(correct, 2048);
    CHECK_EQ_U32(wrong, 8);
    CHECK(wrong <= FEETECH_POSITION_MAX); /* plausible, which is the danger */
}

/* ---------------------------------------------------------------------------
 * Feetech conversions
 * -------------------------------------------------------------------------*/

TEST(feetech_position_endpoints_map_to_a_full_turn)
{
    CHECK_EQ_U32(feetech_position_to_millidegrees(0), 0);
    CHECK_EQ_U32(feetech_position_to_millidegrees(FEETECH_POSITION_MAX),
                 FEETECH_RANGE_MILLIDEGREES);
}

TEST(feetech_position_survives_a_round_trip)
{
    for (unsigned position = 0; position <= FEETECH_POSITION_MAX; position++) {
        const uint32_t md = feetech_position_to_millidegrees((uint16_t)position);
        const uint16_t back = feetech_millidegrees_to_position(md);
        if (back != position) {
            printf("    position %u became %u via %u millidegrees\n",
                   position, back, md);
            CHECK(false);
            return;
        }
    }
}

TEST(feetech_angles_beyond_a_full_turn_clamp)
{
    CHECK_EQ_INT(feetech_millidegrees_to_position(FEETECH_RANGE_MILLIDEGREES),
                 FEETECH_POSITION_MAX);
    CHECK_EQ_INT(feetech_millidegrees_to_position(0xFFFFFFFFu), FEETECH_POSITION_MAX);
}

TEST(feetech_signed_magnitude_uses_bit_fifteen_for_direction)
{
    /* A different bit from the AX-12's, which is why the two decoders are
       separate functions rather than one shared one. */
    CHECK_EQ_INT(feetech_decode_signed_magnitude(0), 0);
    CHECK_EQ_INT(feetech_decode_signed_magnitude(1000), 1000);
    CHECK_EQ_INT(feetech_decode_signed_magnitude(0x8000 | 1000), -1000);
    CHECK_EQ_INT(feetech_decode_signed_magnitude(0x8000), 0);
}

TEST(feetech_signed_magnitude_spans_the_full_field_without_changing_sign)
{
    /* The magnitude field is 15 bits, so its largest value is exactly
       INT16_MAX and neither sign can overflow. */
    const int16_t decoded = feetech_decode_signed_magnitude(0x7FFF);
    CHECK(decoded > 0);
    CHECK_EQ_INT(decoded, 32767);

    const int16_t negative = feetech_decode_signed_magnitude(0xFFFF);
    CHECK(negative < 0);
    CHECK_EQ_INT(negative, -32767);
}

/*
 * Both position conversions must return the *nearest* angle, not merely one
 * within a step. Checked against the exact rational value rather than by
 * repeating the implementation's own formula, so dropping the rounding term
 * cannot pass.
 */
static void check_nearest(const char *what, uint32_t millidegrees, uint32_t position,
                          uint32_t steps, uint32_t range)
{
    const uint64_t scaled = (uint64_t)millidegrees * steps;
    const uint64_t exact = (uint64_t)position * range;
    const uint64_t error = scaled > exact ? scaled - exact : exact - scaled;

    if (error * 2u > steps) {
        printf("    %s: position %u gave %u millidegrees, off by more than half a step\n",
               what, position, millidegrees);
        CHECK(false);
    }
}

TEST(ax12_position_conversion_rounds_to_nearest)
{
    for (unsigned position = 0; position <= AX12_POSITION_MAX; position++) {
        check_nearest("ax12", ax12_position_to_millidegrees((uint16_t)position),
                      position, AX12_POSITION_MAX, AX12_RANGE_MILLIDEGREES);
        if (test_failures > 0) {
            return;
        }
    }
}

TEST(feetech_position_conversion_rounds_to_nearest)
{
    for (unsigned position = 0; position <= FEETECH_POSITION_MAX; position++) {
        check_nearest("feetech", feetech_position_to_millidegrees((uint16_t)position),
                      position, FEETECH_POSITION_MAX, FEETECH_RANGE_MILLIDEGREES);
        if (test_failures > 0) {
            return;
        }
    }
}

TEST(the_ax12_and_feetech_direction_bits_do_not_coincide)
{
    /* Decoding one family's reading with the other's decoder must not silently
       agree, or a copy-paste between the two components would go unnoticed. */
    const uint16_t raw = 0x8000 | 500;
    CHECK_EQ_INT(feetech_decode_signed_magnitude(raw), -500);
    CHECK_EQ_INT(ax12_decode_signed_magnitude(raw), 500); /* reads as positive */
}

/* ---------------------------------------------------------------------------
 * Baud rate conversion
 *
 * The failure this guards against is a mismatch: a servo running at one rate
 * and a host clocked at another. Both ends are set from these functions, so a
 * wrong answer here is a bus that does not work at all — or, worse, one that
 * works until it is warm.
 * -------------------------------------------------------------------------*/

TEST(ax12_baud_divisors_give_the_datasheet_rates)
{
    /* Address 4 in the AX-12 control table, as the datasheet tabulates it. */
    CHECK_EQ_U32(ax12_baud_value_to_rate(1), 1000000);
    CHECK_EQ_U32(ax12_baud_value_to_rate(3), 500000);
    CHECK_EQ_U32(ax12_baud_value_to_rate(4), 400000);
    CHECK_EQ_U32(ax12_baud_value_to_rate(7), 250000);
    CHECK_EQ_U32(ax12_baud_value_to_rate(9), 200000);

    /* The four the datasheet gives rounded names. These are the real rates. */
    CHECK_EQ_U32(ax12_baud_value_to_rate(16), 117647);   /* "115200" */
    CHECK_EQ_U32(ax12_baud_value_to_rate(34), 57142);    /* "57600" */
    CHECK_EQ_U32(ax12_baud_value_to_rate(103), 19230);   /* "19200" */
    CHECK_EQ_U32(ax12_baud_value_to_rate(207), 9615);    /* "9600" */
}

TEST(ax12_baud_value_zero_is_not_a_rate)
{
    /* 2 Mbaud, which the servo's UART does not do; the framework refuses it
       rather than writing a divisor the servo will not honour. */
    CHECK_EQ_U32(ax12_baud_value_to_rate(0), 0);
    CHECK_EQ_U32(ax12_baud_value_to_rate(255), 0);
}

TEST(ax12_datasheet_names_and_real_rates_select_the_same_divisor)
{
    /*
     * Both spellings have to work. A caller reading the datasheet asks for
     * 115200; a caller reading the rate back out of the framework asks for
     * 117647. Landing on different divisors would leave the two ends 2% apart.
     */
    uint8_t from_name = 0, from_rate = 0;
    CHECK(ax12_rate_to_baud_value(115200, &from_name));
    CHECK(ax12_rate_to_baud_value(117647, &from_rate));
    CHECK_EQ_INT(from_name, 16);
    CHECK_EQ_INT(from_rate, 16);

    CHECK(ax12_rate_to_baud_value(57600, &from_name));
    CHECK_EQ_INT(from_name, 34);
    CHECK(ax12_rate_to_baud_value(9600, &from_name));
    CHECK_EQ_INT(from_name, 207);
    CHECK(ax12_rate_to_baud_value(19200, &from_name));
    CHECK_EQ_INT(from_name, 103);
    CHECK(ax12_rate_to_baud_value(1000000, &from_name));
    CHECK_EQ_INT(from_name, 1);
}

TEST(ax12_unreachable_rates_are_refused_rather_than_rounded)
{
    uint8_t value = 0xAA;

    /* Above the top divisor: rounding would silently hand back 1 Mbaud. */
    CHECK(!ax12_rate_to_baud_value(2000000, &value));
    CHECK(!ax12_rate_to_baud_value(1500000, &value));

    /* Below the bottom one. */
    CHECK(!ax12_rate_to_baud_value(4800, &value));
    CHECK(!ax12_rate_to_baud_value(0, &value));

    /* In range, but no divisor lands within 3%: 700000 sits between the
       666666 and 1000000 steps, 5% from the nearer of them. */
    CHECK(!ax12_rate_to_baud_value(700000, &value));

    /* Every refusal left the caller's variable alone. */
    CHECK_EQ_INT(value, 0xAA);

    /*
     * Inside the tolerance the nearest divisor wins even when the rate is not
     * one the datasheet names: 150000 resolves to the divisor for 153846, and
     * because the caller clocks the host from ax12_baud_value_to_rate() the
     * two ends still agree exactly.
     */
    CHECK(ax12_rate_to_baud_value(150000, &value));
    CHECK_EQ_U32(ax12_baud_value_to_rate(value), 153846);
}

TEST(every_rate_in_the_ax12_sweep_table_round_trips)
{
    /*
     * The sweep sets the host to each of these in turn, so each has to convert
     * back to the divisor that produces it exactly. An entry that did not
     * would make the sweep miss servos that are in fact there.
     */
    size_t count = 0;
    const uint32_t *rates = ax12_baud_rate_table(&count);

    CHECK(rates != NULL);
    CHECK(count > 0);

    for (size_t i = 0; i < count; i++) {
        uint8_t value = 0;
        if (!ax12_rate_to_baud_value(rates[i], &value)) {
            printf("    rate %u has no divisor\n", (unsigned)rates[i]);
            CHECK(false);
            continue;
        }
        CHECK_EQ_U32(ax12_baud_value_to_rate(value), rates[i]);
    }

    /* Fastest first, so a sweep tries the factory setting before the slow
       rates whose timeouts dominate the sweep's duration. */
    for (size_t i = 1; i < count; i++) {
        CHECK(rates[i] < rates[i - 1]);
    }
}

TEST(feetech_baud_indices_give_the_datasheet_rates)
{
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_1000000), 1000000);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_500000), 500000);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_250000), 250000);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_128000), 128000);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_115200), 115200);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_76800), 76800);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_57600), 57600);
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_38400), 38400);

    /* Past the end of the table, rather than an eighth entry nobody has. */
    CHECK_EQ_U32(feetech_baud_index_to_rate(FEETECH_BAUD_INDEX_MAX + 1u), 0);
    CHECK_EQ_U32(feetech_baud_index_to_rate(255), 0);
}

TEST(every_rate_in_the_feetech_sweep_table_round_trips)
{
    size_t count = 0;
    const uint32_t *rates = feetech_baud_rate_table(&count);

    CHECK(rates != NULL);
    CHECK_EQ_U32((uint32_t)count, FEETECH_BAUD_INDEX_MAX + 1u);

    for (size_t i = 0; i < count; i++) {
        uint8_t index = 0xFF;
        if (!feetech_rate_to_baud_index(rates[i], &index)) {
            printf("    rate %u has no index\n", (unsigned)rates[i]);
            CHECK(false);
            continue;
        }
        CHECK_EQ_U32((uint32_t)index, (uint32_t)i);
        CHECK_EQ_U32(feetech_baud_index_to_rate(index), rates[i]);
    }

    for (size_t i = 1; i < count; i++) {
        CHECK(rates[i] < rates[i - 1]);
    }
}

TEST(feetech_rates_off_the_table_are_refused)
{
    uint8_t index = 0xAA;

    CHECK(!feetech_rate_to_baud_index(9600, &index));
    CHECK(!feetech_rate_to_baud_index(2000000, &index));
    CHECK(!feetech_rate_to_baud_index(200000, &index));
    CHECK(!feetech_rate_to_baud_index(0, &index));
    CHECK_EQ_INT(index, 0xAA);

    /* Close enough counts: 125000 is 2.3% off the table's 128000, which is
       what a host UART's quantisation looks like. */
    CHECK(feetech_rate_to_baud_index(125000, &index));
    CHECK_EQ_INT(index, FEETECH_BAUD_INDEX_128000);
}

TEST(the_two_families_number_their_baud_registers_differently)
{
    /*
     * A Feetech register holds a table index and an AX-12 register holds a
     * divisor, so the same number means different rates. Writing 4 to both
     * asks for 115200 on one and 400000 on the other — which is why neither
     * component exposes the raw value as a rate.
     */
    CHECK_EQ_U32(feetech_baud_index_to_rate(4), 115200);
    CHECK_EQ_U32(ax12_baud_value_to_rate(4), 400000);
}

TEST_MAIN(
    RUN(ax12_two_byte_registers_are_two_bytes);
    RUN(ax12_single_byte_registers_are_one_byte);
    RUN(the_high_half_of_an_ax12_word_register_is_not_a_register);
    RUN(an_unknown_ax12_address_has_no_width_or_name);
    RUN(the_ax12_eeprom_boundary_is_where_the_datasheet_says);
    RUN(every_named_ax12_register_has_a_width);

    RUN(ax12_position_endpoints_map_to_the_ends_of_the_arc);
    RUN(ax12_position_centre_is_the_middle_of_the_arc);
    RUN(ax12_position_survives_a_round_trip);
    RUN(ax12_angles_beyond_the_arc_clamp_rather_than_wrap);
    RUN(ax12_positions_above_the_range_clamp);
    RUN(ax12_signed_magnitude_uses_bit_ten_for_direction);
    RUN(ax12_signed_magnitude_ignores_bits_above_the_field);
    RUN(ax12_voltage_is_tenths_of_a_volt);

    RUN(feetech_two_byte_registers_are_two_bytes);
    RUN(feetech_single_byte_registers_are_one_byte);
    RUN(feetech_and_ax12_control_tables_really_are_different);
    RUN(every_named_feetech_register_has_a_width);
    RUN(the_feetech_eeprom_boundary_is_where_the_datasheet_says);

    RUN(the_two_feetech_families_disagree_about_byte_order);
    RUN(reading_a_position_with_the_wrong_byte_order_is_silently_wrong);

    RUN(feetech_position_endpoints_map_to_a_full_turn);
    RUN(feetech_position_survives_a_round_trip);
    RUN(feetech_angles_beyond_a_full_turn_clamp);
    RUN(feetech_signed_magnitude_uses_bit_fifteen_for_direction);
    RUN(feetech_signed_magnitude_spans_the_full_field_without_changing_sign);
    RUN(ax12_position_conversion_rounds_to_nearest);
    RUN(feetech_position_conversion_rounds_to_nearest);
    RUN(the_ax12_and_feetech_direction_bits_do_not_coincide);

    RUN(ax12_baud_divisors_give_the_datasheet_rates);
    RUN(ax12_baud_value_zero_is_not_a_rate);
    RUN(ax12_datasheet_names_and_real_rates_select_the_same_divisor);
    RUN(ax12_unreachable_rates_are_refused_rather_than_rounded);
    RUN(every_rate_in_the_ax12_sweep_table_round_trips);
    RUN(feetech_baud_indices_give_the_datasheet_rates);
    RUN(every_rate_in_the_feetech_sweep_table_round_trips);
    RUN(feetech_rates_off_the_table_are_refused);
    RUN(the_two_families_number_their_baud_registers_differently);
)
