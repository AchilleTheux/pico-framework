/*
 * Host-side tests for Dynamixel Protocol 1.0 packet encoding and decoding.
 *
 * Several cases are the worked examples straight out of the AX-12 datasheet,
 * checksum included. Those are the highest-value tests here: they check the
 * implementation against the specification rather than against itself, so a
 * consistent misunderstanding of the format cannot pass.
 */

#include <string.h>

#include "test.h"

#include "servo_protocol.h"

static void check_bytes(const char *what, const uint8_t *actual, size_t actual_len,
                        const uint8_t *expected, size_t expected_len)
{
    if (actual_len != expected_len) {
        printf("    %s: expected %u bytes, got %u\n", what,
               (unsigned)expected_len, (unsigned)actual_len);
        CHECK_EQ_INT(actual_len, expected_len);
        return;
    }
    for (size_t i = 0; i < expected_len; i++) {
        if (actual[i] != expected[i]) {
            printf("    %s: byte %u expected 0x%02X, got 0x%02X\n", what,
                   (unsigned)i, expected[i], actual[i]);
            CHECK(false);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Checksum
 * -------------------------------------------------------------------------*/

TEST(checksum_matches_the_datasheet_example)
{
    /* AX-12 datasheet: writing ID 1 to a servo whose ID is 0.
       FF FF 00 04 03 03 01 F4 — checksum over 00 04 03 03 01 is 0xF4. */
    static const uint8_t body[] = { 0x00, 0x04, 0x03, 0x03, 0x01 };
    CHECK_EQ_INT(servo_protocol_checksum(body, sizeof(body)), 0xF4);
}

TEST(checksum_is_the_complement_of_the_sum)
{
    static const uint8_t body[] = { 0x01, 0x02, 0x03 };
    CHECK_EQ_INT(servo_protocol_checksum(body, sizeof(body)), (uint8_t)~0x06);
}

TEST(checksum_wraps_rather_than_overflowing)
{
    /* 0xFF + 0xFF + 0x02 = 0x200, truncated to 0x00, complement 0xFF. */
    static const uint8_t body[] = { 0xFF, 0xFF, 0x02 };
    CHECK_EQ_INT(servo_protocol_checksum(body, sizeof(body)), 0xFF);
}

/* ---------------------------------------------------------------------------
 * Building
 * -------------------------------------------------------------------------*/

TEST(ping_matches_the_datasheet_example)
{
    /* AX-12 datasheet: ping servo 1 is FF FF 01 02 01 FB. */
    static const uint8_t expected[] = { 0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_ping(packet, sizeof(packet), &written, 1),
                 SERVO_PROTOCOL_OK);
    check_bytes("ping", packet, written, expected, sizeof(expected));
}

TEST(read_matches_the_datasheet_example)
{
    /* Read 1 byte of internal temperature (register 0x2B) from servo 1:
       FF FF 01 04 02 2B 01 CC. */
    static const uint8_t expected[] = { 0xFF, 0xFF, 0x01, 0x04, 0x02, 0x2B, 0x01, 0xCC };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_read(packet, sizeof(packet), &written, 1, 0x2B, 1),
                 SERVO_PROTOCOL_OK);
    check_bytes("read", packet, written, expected, sizeof(expected));
}

TEST(write_matches_the_datasheet_example)
{
    /* Set the ID of the servo currently at 0 to 1:
       FF FF 00 04 03 03 01 F4. */
    static const uint8_t expected[] = { 0xFF, 0xFF, 0x00, 0x04, 0x03, 0x03, 0x01, 0xF4 };
    static const uint8_t data[] = { 0x01 };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_write(packet, sizeof(packet), &written,
                                            0x00, 0x03, data, sizeof(data)),
                 SERVO_PROTOCOL_OK);
    check_bytes("write", packet, written, expected, sizeof(expected));
}

TEST(a_goal_position_write_is_little_endian_for_ax12)
{
    /*
     * Goal position 0x0200 to servo 1: the low byte goes first, so the
     * parameters read 1E 00 02. A big-endian encoding would move the servo to
     * position 2 instead of 512 and look almost right.
     */
    static const uint8_t expected_params[] = { 0x1E, 0x00, 0x02 };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_write_value(packet, sizeof(packet), &written,
                                                  1, 0x1E, 0x0200, 2,
                                                  SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_OK);
    CHECK_EQ_INT(written, 9);
    check_bytes("params", &packet[5], 3, expected_params, sizeof(expected_params));
}

TEST(a_goal_position_write_is_big_endian_when_asked)
{
    static const uint8_t expected_params[] = { 0x1E, 0x02, 0x00 };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_write_value(packet, sizeof(packet), &written,
                                                  1, 0x1E, 0x0200, 2,
                                                  SERVO_ENDIAN_BIG),
                 SERVO_PROTOCOL_OK);
    check_bytes("params", &packet[5], 3, expected_params, sizeof(expected_params));
}

TEST(a_built_packet_always_carries_a_valid_checksum)
{
    /* Whatever is built must parse back, so the two halves cannot drift. */
    for (unsigned id = 0; id < 256; id += 17) {
        for (unsigned count = 1; count <= 8; count++) {
            uint8_t data[8];
            for (unsigned i = 0; i < count; i++) {
                data[i] = (uint8_t)(id + i * 31u);
            }

            uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
            size_t written = 0;
            if (servo_protocol_build_write(packet, sizeof(packet), &written,
                                           (uint8_t)id, 0x1E, data,
                                           (uint8_t)count) != SERVO_PROTOCOL_OK) {
                CHECK(false);
                return;
            }

            /* Parsing treats byte 4 as the error field rather than the
               instruction, but the framing and checksum are the same. */
            servo_status_packet_t parsed;
            if (servo_protocol_parse_status(packet, written, &parsed) != SERVO_PROTOCOL_OK) {
                printf("    id=%u count=%u did not round-trip\n", id, count);
                CHECK(false);
                return;
            }
            if (parsed.id != (uint8_t)id || parsed.packet_size != written) {
                CHECK_EQ_INT(parsed.id, id);
                return;
            }
        }
    }
}

TEST(building_into_a_short_buffer_fails_without_writing)
{
    uint8_t packet[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
    size_t written = 123;

    CHECK_EQ_INT(servo_protocol_build_ping(packet, sizeof(packet), &written, 1),
                 SERVO_PROTOCOL_ERR_BUFFER_TOO_SMALL);
    CHECK_EQ_INT(written, 0);
    CHECK_EQ_INT(packet[0], 0xAA); /* untouched */
}

TEST(building_rejects_more_parameters_than_the_format_allows)
{
    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t data[SERVO_PROTOCOL_MAX_PARAMS + 8];
    memset(data, 0, sizeof(data));
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build(packet, sizeof(packet), &written, 1,
                                      SERVO_INST_WRITE, data,
                                      (uint8_t)(SERVO_PROTOCOL_MAX_PARAMS + 1)),
                 SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS);
    CHECK_EQ_INT(written, 0);
}

TEST(a_write_leaves_room_for_the_register_address)
{
    /* The register byte occupies one of the parameter slots, so the largest
       payload is one less than the parameter limit. */
    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t data[SERVO_PROTOCOL_MAX_PARAMS];
    memset(data, 0x5A, sizeof(data));
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_write(packet, sizeof(packet), &written, 1, 0,
                                            data, (uint8_t)(SERVO_PROTOCOL_MAX_PARAMS - 1)),
                 SERVO_PROTOCOL_OK);

    CHECK_EQ_INT(servo_protocol_build_write(packet, sizeof(packet), &written, 1, 0,
                                            data, (uint8_t)SERVO_PROTOCOL_MAX_PARAMS),
                 SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS);
}

TEST(a_zero_length_read_is_rejected)
{
    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;
    CHECK_EQ_INT(servo_protocol_build_read(packet, sizeof(packet), &written, 1, 0x24, 0),
                 SERVO_PROTOCOL_ERR_INVALID_ARG);
}

TEST(an_odd_value_width_is_rejected)
{
    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;
    CHECK_EQ_INT(servo_protocol_build_write_value(packet, sizeof(packet), &written,
                                                  1, 0x1E, 5, 3, SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_ERR_INVALID_ARG);
}

/* ---------------------------------------------------------------------------
 * Parsing
 * -------------------------------------------------------------------------*/

TEST(an_unsuppressed_echo_parses_as_a_plausible_reply)
{
    /*
     * Why half_duplex_uart defaults to consuming the echo.
     *
     * On a shared wire the transmitter's own bytes come back. If they are not
     * consumed, the next read sees the request itself — and a request is a
     * well-formed packet with a valid checksum, so it parses cleanly as a
     * status reply from the right servo. The caller gets a confident wrong
     * answer rather than an error, which is the worst failure mode available.
     *
     * Here: a READ of present_position (0x24, 2 bytes) from servo 1, parsed as
     * though it were the reply.
     */
    uint8_t request[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_read(request, sizeof(request), &written,
                                           1, 0x24, 2),
                 SERVO_PROTOCOL_OK);

    servo_status_packet_t parsed;
    const servo_protocol_result_t result =
        servo_protocol_parse_status(request, written, &parsed);

    /* It parses. The checksum is valid because we computed it ourselves. */
    CHECK_EQ_INT(result, SERVO_PROTOCOL_OK);
    CHECK_EQ_INT(parsed.id, 1);
    CHECK_EQ_INT(parsed.param_count, 2);

    /* And it decodes to a position that is entirely in range: 0x0224 = 548,
       indistinguishable from a servo sitting near the middle of its travel. */
    const uint32_t bogus_position =
        servo_protocol_decode_value(parsed.params, 2, SERVO_ENDIAN_LITTLE);
    CHECK_EQ_U32(bogus_position, 0x0224);
    CHECK(bogus_position <= 1023); /* plausible for an AX-12, which is the danger */
}

TEST(a_status_packet_from_the_datasheet_parses)
{
    /* Reply to reading temperature: servo 1, no error, one byte 0x20 (32 C).
       FF FF 01 03 00 20 DB. */
    static const uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB };

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(reply, sizeof(reply), &parsed),
                 SERVO_PROTOCOL_OK);
    CHECK_EQ_INT(parsed.id, 1);
    CHECK_EQ_INT(parsed.error, 0);
    CHECK_EQ_INT(parsed.param_count, 1);
    CHECK_EQ_INT(parsed.params[0], 0x20);
    CHECK_EQ_INT(parsed.packet_size, sizeof(reply));
}

TEST(a_ping_reply_carries_no_parameters)
{
    static const uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC };

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(reply, sizeof(reply), &parsed),
                 SERVO_PROTOCOL_OK);
    CHECK_EQ_INT(parsed.param_count, 0);
    CHECK(parsed.params == NULL);
}

TEST(the_error_byte_is_reported)
{
    uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x02,
                        SERVO_ERROR_OVERHEATING | SERVO_ERROR_OVERLOAD, 0x00 };
    reply[5] = servo_protocol_checksum(&reply[2], 3);

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(reply, sizeof(reply), &parsed),
                 SERVO_PROTOCOL_OK);
    CHECK(parsed.error & SERVO_ERROR_OVERHEATING);
    CHECK(parsed.error & SERVO_ERROR_OVERLOAD);
    CHECK(!(parsed.error & SERVO_ERROR_CHECKSUM));
}

TEST(a_corrupted_checksum_is_rejected)
{
    uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB };
    reply[6] ^= 0x01;

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(reply, sizeof(reply), &parsed),
                 SERVO_PROTOCOL_ERR_BAD_CHECKSUM);
}

TEST(a_corrupted_payload_is_caught_by_the_checksum)
{
    /* The failure this protects against: a bit flip on the bus turning a
       position reading into a different, plausible one. */
    uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB };
    reply[5] = 0x21;

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(reply, sizeof(reply), &parsed),
                 SERVO_PROTOCOL_ERR_BAD_CHECKSUM);
}

TEST(a_truncated_packet_is_incomplete_not_malformed)
{
    static const uint8_t full[] = { 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB };

    /* Every prefix must say "keep waiting" rather than "not a packet", so a
       caller reading byte by byte does not give up early. */
    for (size_t len = 0; len < sizeof(full); len++) {
        servo_status_packet_t parsed;
        const servo_protocol_result_t result =
            servo_protocol_parse_status(full, len, &parsed);
        if (result != SERVO_PROTOCOL_ERR_INCOMPLETE) {
            printf("    prefix of %u bytes gave %s\n", (unsigned)len,
                   servo_protocol_result_name(result));
            CHECK(false);
            return;
        }
    }

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(full, sizeof(full), &parsed),
                 SERVO_PROTOCOL_OK);
}

TEST(a_missing_header_is_rejected)
{
    static const uint8_t junk[] = { 0x00, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB };

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(junk, sizeof(junk), &parsed),
                 SERVO_PROTOCOL_ERR_BAD_HEADER);
}

TEST(an_impossible_length_is_rejected)
{
    /* LENGTH counts the error byte and the checksum, so it can never be
       below 2. Accepting 0 or 1 would underflow the parameter count. */
    for (uint8_t length = 0; length < 2; length++) {
        uint8_t reply[] = { 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00 };
        reply[3] = length;

        servo_status_packet_t parsed;
        const servo_protocol_result_t result =
            servo_protocol_parse_status(reply, sizeof(reply), &parsed);
        if (result != SERVO_PROTOCOL_ERR_BAD_HEADER) {
            printf("    length %u gave %s\n", length,
                   servo_protocol_result_name(result));
            CHECK(false);
            return;
        }
    }
}

TEST(trailing_bytes_after_a_packet_are_ignored)
{
    /* A reply followed by the start of another must still parse, and report
       its own size so the caller can advance past it. */
    static const uint8_t stream[] = {
        0xFF, 0xFF, 0x01, 0x03, 0x00, 0x20, 0xDB,
        0xFF, 0xFF, 0x02,
    };

    servo_status_packet_t parsed;
    CHECK_EQ_INT(servo_protocol_parse_status(stream, sizeof(stream), &parsed),
                 SERVO_PROTOCOL_OK);
    CHECK_EQ_INT(parsed.packet_size, 7);
}

/* ---------------------------------------------------------------------------
 * Values
 * -------------------------------------------------------------------------*/

TEST(values_round_trip_in_both_byte_orders)
{
    static const uint32_t values[] = { 0, 1, 0x00FF, 0x0100, 0x1234, 0xFFFF };

    for (unsigned i = 0; i < count_of_(values); i++) {
        uint8_t buffer[4];

        servo_protocol_encode_value(buffer, values[i], 2, SERVO_ENDIAN_LITTLE);
        CHECK_EQ_U32(servo_protocol_decode_value(buffer, 2, SERVO_ENDIAN_LITTLE),
                     values[i]);

        servo_protocol_encode_value(buffer, values[i], 2, SERVO_ENDIAN_BIG);
        CHECK_EQ_U32(servo_protocol_decode_value(buffer, 2, SERVO_ENDIAN_BIG),
                     values[i]);
    }
}

TEST(the_two_byte_orders_are_actually_different)
{
    /* A test that passes under either convention would be worthless. */
    uint8_t little[2];
    uint8_t big[2];

    servo_protocol_encode_value(little, 0x1234, 2, SERVO_ENDIAN_LITTLE);
    servo_protocol_encode_value(big, 0x1234, 2, SERVO_ENDIAN_BIG);

    CHECK_EQ_INT(little[0], 0x34);
    CHECK_EQ_INT(little[1], 0x12);
    CHECK_EQ_INT(big[0], 0x12);
    CHECK_EQ_INT(big[1], 0x34);
}

TEST(single_byte_values_are_order_independent)
{
    const uint8_t byte = 0xA5;
    CHECK_EQ_U32(servo_protocol_decode_value(&byte, 1, SERVO_ENDIAN_LITTLE), 0xA5);
    CHECK_EQ_U32(servo_protocol_decode_value(&byte, 1, SERVO_ENDIAN_BIG), 0xA5);
}

TEST(four_byte_values_round_trip)
{
    uint8_t buffer[4];
    servo_protocol_encode_value(buffer, 0xDEADBEEF, 4, SERVO_ENDIAN_LITTLE);
    CHECK_EQ_U32(servo_protocol_decode_value(buffer, 4, SERVO_ENDIAN_LITTLE), 0xDEADBEEF);
    CHECK_EQ_INT(buffer[0], 0xEF);
}

TEST(an_unsupported_width_decodes_to_zero)
{
    static const uint8_t buffer[4] = { 1, 2, 3, 4 };
    CHECK_EQ_U32(servo_protocol_decode_value(buffer, 3, SERVO_ENDIAN_LITTLE), 0);
    CHECK_EQ_U32(servo_protocol_decode_value(NULL, 2, SERVO_ENDIAN_LITTLE), 0);
}

/* ---------------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------------*/

TEST(no_error_bits_describes_as_none)
{
    char buffer[64];
    CHECK_EQ_STR(servo_protocol_describe_error(0, buffer, sizeof(buffer)), "none");
}

TEST(set_error_bits_are_named)
{
    char buffer[64];
    servo_protocol_describe_error(SERVO_ERROR_OVERHEATING | SERVO_ERROR_OVERLOAD,
                                  buffer, sizeof(buffer));
    CHECK_EQ_STR(buffer, "overheating, overload");
}

TEST(a_short_description_buffer_does_not_overflow)
{
    char buffer[8];
    memset(buffer, 0x7F, sizeof(buffer));
    servo_protocol_describe_error(0xFF, buffer, sizeof(buffer));

    /* Must still be a valid string within the buffer. */
    CHECK(memchr(buffer, '\0', sizeof(buffer)) != NULL);
    CHECK(strlen(buffer) < sizeof(buffer));
}

TEST(a_one_byte_description_buffer_is_survivable)
{
    char buffer[1];
    servo_protocol_describe_error(0xFF, buffer, sizeof(buffer));
    CHECK_EQ_INT(buffer[0], '\0');
}

/* ---------------------------------------------------------------------------
 * Sync write
 *
 * One packet that writes a different value to the same register on many
 * servos, so they all start on the same byte rather than several milliseconds
 * apart. The expected bytes below were derived from the format, not captured
 * from this code.
 * -------------------------------------------------------------------------*/

TEST(a_sync_write_matches_the_format)
{
    /* Goal position on servos 1 and 2, two bytes each, little-endian:
       FF FF FE 0A 83 1E 02 01 00 01 02 00 02 4E */
    static const uint8_t expected[] = {
        0xFF, 0xFF, 0xFE, 0x0A, 0x83, 0x1E, 0x02,
        0x01, 0x00, 0x01,
        0x02, 0x00, 0x02,
        0x4E,
    };
    static const servo_sync_target_t targets[] = {
        { .id = 1, .value = 0x0100 },
        { .id = 2, .value = 0x0200 },
    };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x1E, 2, targets, 2,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_OK);
    check_bytes("sync write", packet, written, expected, sizeof(expected));
}

TEST(a_sync_write_goes_to_the_broadcast_id)
{
    /* It must, or only one servo would act on it. */
    static const servo_sync_target_t targets[] = { { .id = 3, .value = 1 } };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;
    servo_protocol_build_sync_write(packet, sizeof(packet), &written, 0x1E, 2,
                                    targets, 1, SERVO_ENDIAN_LITTLE);

    CHECK_EQ_INT(packet[2], SERVO_PROTOCOL_BROADCAST_ID);
    CHECK_EQ_INT(packet[4], SERVO_INST_SYNC_WRITE);
}

TEST(a_sync_write_carries_a_valid_checksum_at_every_size)
{
    /* Its length field and checksum both depend on the servo count, so both
       are checked across the range rather than at one size. */
    servo_sync_target_t targets[42];
    for (unsigned i = 0; i < count_of_(targets); i++) {
        targets[i].id = (uint8_t)(i + 1u);
        targets[i].value = (uint16_t)(i * 97u);
    }

    for (uint8_t count = 1; count <= count_of_(targets); count++) {
        if (servo_protocol_sync_write_params(2, count) > SERVO_PROTOCOL_MAX_PARAMS) {
            break;
        }

        uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
        size_t written = 0;
        if (servo_protocol_build_sync_write(packet, sizeof(packet), &written, 0x1E, 2,
                                            targets, count, SERVO_ENDIAN_LITTLE)
                != SERVO_PROTOCOL_OK) {
            printf("    %u servos would not build\n", count);
            CHECK(false);
            return;
        }

        /* Parses back, so the length and checksum agree with the payload. */
        servo_status_packet_t parsed;
        if (servo_protocol_parse_status(packet, written, &parsed) != SERVO_PROTOCOL_OK) {
            printf("    %u servos did not round-trip\n", count);
            CHECK(false);
            return;
        }
        if (parsed.packet_size != written) {
            CHECK_EQ_INT(parsed.packet_size, written);
            return;
        }
    }
}

TEST(a_sync_write_encodes_each_value_in_the_bus_byte_order)
{
    static const servo_sync_target_t targets[] = { { .id = 1, .value = 0x1234 } };

    uint8_t little[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    uint8_t big[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    servo_protocol_build_sync_write(little, sizeof(little), &written, 0x1E, 2,
                                    targets, 1, SERVO_ENDIAN_LITTLE);
    servo_protocol_build_sync_write(big, sizeof(big), &written, 0x1E, 2,
                                    targets, 1, SERVO_ENDIAN_BIG);

    /* Parameters start at index 5: reg, width, id, then the value. */
    CHECK_EQ_INT(little[8], 0x34);
    CHECK_EQ_INT(little[9], 0x12);
    CHECK_EQ_INT(big[8], 0x12);
    CHECK_EQ_INT(big[9], 0x34);
}

TEST(a_sync_write_of_one_byte_values_is_compact)
{
    /* Torque enable across several servos: one byte each. */
    static const servo_sync_target_t targets[] = {
        { .id = 1, .value = 1 }, { .id = 2, .value = 1 }, { .id = 3, .value = 0 },
    };

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;
    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x18, 1, targets, 3,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_OK);

    /* 2 + 2*3 = 8 parameters, so 14 bytes on the wire. */
    CHECK_EQ_INT(written, servo_protocol_packet_size(8));
    CHECK_EQ_INT(packet[6], 1); /* the width the servos will split by */
}

TEST(too_many_servos_for_one_packet_is_rejected)
{
    servo_sync_target_t targets[128];
    for (unsigned i = 0; i < count_of_(targets); i++) {
        targets[i].id = (uint8_t)i;
        targets[i].value = 0;
    }

    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 123;

    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x1E, 2, targets, 100,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_ERR_TOO_MANY_PARAMS);
    CHECK_EQ_INT(written, 0);
}

TEST(a_degenerate_sync_write_is_rejected)
{
    static const servo_sync_target_t targets[] = { { .id = 1, .value = 0 } };
    uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
    size_t written = 0;

    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x1E, 2, targets, 0,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_ERR_INVALID_ARG);
    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x1E, 3, targets, 1,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_ERR_INVALID_ARG);
    CHECK_EQ_INT(servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                 0x1E, 2, NULL, 1,
                                                 SERVO_ENDIAN_LITTLE),
                 SERVO_PROTOCOL_ERR_INVALID_ARG);
}

TEST(the_predicted_parameter_count_matches_what_is_built)
{
    /* Callers use this to check a batch will fit before trying. */
    servo_sync_target_t targets[8];
    for (unsigned i = 0; i < count_of_(targets); i++) {
        targets[i].id = (uint8_t)(i + 1u);
        targets[i].value = i;
    }

    static const uint8_t widths[] = { 1, 2, 4 };
    for (unsigned w = 0; w < count_of_(widths); w++) {
        for (uint8_t count = 1; count <= count_of_(targets); count++) {
            uint8_t packet[SERVO_PROTOCOL_MAX_PACKET_SIZE];
            size_t written = 0;
            if (servo_protocol_build_sync_write(packet, sizeof(packet), &written,
                                                0x1E, widths[w], targets, count,
                                                SERVO_ENDIAN_LITTLE) != SERVO_PROTOCOL_OK) {
                continue;
            }
            const size_t predicted =
                servo_protocol_sync_write_params(widths[w], count);
            if (written != servo_protocol_packet_size((uint8_t)predicted)) {
                printf("    width %u count %u: predicted %u params, packet %u bytes\n",
                       widths[w], count, (unsigned)predicted, (unsigned)written);
                CHECK(false);
                return;
            }
        }
    }
}

TEST_MAIN(
    RUN(checksum_matches_the_datasheet_example);
    RUN(checksum_is_the_complement_of_the_sum);
    RUN(checksum_wraps_rather_than_overflowing);

    RUN(ping_matches_the_datasheet_example);
    RUN(read_matches_the_datasheet_example);
    RUN(write_matches_the_datasheet_example);
    RUN(a_goal_position_write_is_little_endian_for_ax12);
    RUN(a_goal_position_write_is_big_endian_when_asked);
    RUN(a_built_packet_always_carries_a_valid_checksum);
    RUN(building_into_a_short_buffer_fails_without_writing);
    RUN(building_rejects_more_parameters_than_the_format_allows);
    RUN(a_write_leaves_room_for_the_register_address);
    RUN(a_zero_length_read_is_rejected);
    RUN(an_odd_value_width_is_rejected);

    RUN(an_unsuppressed_echo_parses_as_a_plausible_reply);
    RUN(a_sync_write_matches_the_format);
    RUN(a_sync_write_goes_to_the_broadcast_id);
    RUN(a_sync_write_carries_a_valid_checksum_at_every_size);
    RUN(a_sync_write_encodes_each_value_in_the_bus_byte_order);
    RUN(a_sync_write_of_one_byte_values_is_compact);
    RUN(too_many_servos_for_one_packet_is_rejected);
    RUN(a_degenerate_sync_write_is_rejected);
    RUN(the_predicted_parameter_count_matches_what_is_built);

    RUN(a_status_packet_from_the_datasheet_parses);
    RUN(a_ping_reply_carries_no_parameters);
    RUN(the_error_byte_is_reported);
    RUN(a_corrupted_checksum_is_rejected);
    RUN(a_corrupted_payload_is_caught_by_the_checksum);
    RUN(a_truncated_packet_is_incomplete_not_malformed);
    RUN(a_missing_header_is_rejected);
    RUN(an_impossible_length_is_rejected);
    RUN(trailing_bytes_after_a_packet_are_ignored);

    RUN(values_round_trip_in_both_byte_orders);
    RUN(the_two_byte_orders_are_actually_different);
    RUN(single_byte_values_are_order_independent);
    RUN(four_byte_values_round_trip);
    RUN(an_unsupported_width_decodes_to_zero);

    RUN(no_error_bits_describes_as_none);
    RUN(set_error_bits_are_named);
    RUN(a_short_description_buffer_does_not_overflow);
    RUN(a_one_byte_description_buffer_is_survivable);
)
