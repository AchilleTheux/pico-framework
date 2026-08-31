/*
 * Host-side tests for Intel HEX decoding.
 *
 * The records below are real: generated independently and checked against the
 * format's own rules, not captured from this parser. The important ones are
 * the address-extension cases, because a 16-bit record address cannot reach
 * RP2040 flash at 0x10000000 and getting the base wrong writes a firmware
 * image to entirely the wrong place.
 */

#include <string.h>

#include "test.h"

#include "hex_parser.h"

static hex_parser_t parser;

static void setup(void)
{
    hex_parser_reset(&parser);
}

static hex_result_t feed(const char *line, hex_record_t *record)
{
    return hex_parser_feed(&parser, line, record);
}

/* ---------------------------------------------------------------------------
 * Well-formed records
 * -------------------------------------------------------------------------*/

TEST(a_data_record_decodes)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_INT(record.type, HEX_RECORD_DATA);
    CHECK_EQ_INT(record.length, 4);
    CHECK_EQ_U32(record.address, 0x0000u);
    CHECK_EQ_INT(record.data[0], 0xDE);
    CHECK_EQ_INT(record.data[1], 0xAD);
    CHECK_EQ_INT(record.data[2], 0xBE);
    CHECK_EQ_INT(record.data[3], 0xEF);
}

TEST(the_textbook_record_decodes)
{
    /* The example that appears in every description of the format. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":10010000214601360121470136007EFE09D2190140", &record), HEX_OK);
    CHECK_EQ_INT(record.type, HEX_RECORD_DATA);
    CHECK_EQ_INT(record.length, 16);
    CHECK_EQ_U32(record.address, 0x0100u);
    CHECK_EQ_INT(record.data[0], 0x21);
    CHECK_EQ_INT(record.data[15], 0x01);
}

TEST(an_end_of_file_record_decodes)
{
    setup();
    hex_record_t record;

    CHECK(!hex_parser_is_complete(&parser));
    CHECK_EQ_INT(feed(":00000001FF", &record), HEX_OK);
    CHECK_EQ_INT(record.type, HEX_RECORD_EOF);
    CHECK(hex_parser_is_complete(&parser));
}

TEST(lowercase_hex_digits_are_accepted)
{
    /* Some tools emit lowercase; rejecting it would be gratuitous. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000deadbeefc4", &record), HEX_OK);
    CHECK_EQ_INT(record.data[0], 0xDE);
}

TEST(surrounding_whitespace_is_tolerated)
{
    /* A line read straight off a serial link keeps its CRLF. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed("  :04000000DEADBEEFC4\r\n", &record), HEX_OK);
    CHECK_EQ_INT(record.length, 4);
}

/* ---------------------------------------------------------------------------
 * Address extension: the part that puts an image at 0x10000000
 * -------------------------------------------------------------------------*/

TEST(an_extended_linear_record_sets_the_upper_address_half)
{
    setup();
    hex_record_t record;

    /* The record a linker emits to reach RP2040 flash. */
    CHECK_EQ_INT(feed(":020000041000EA", &record), HEX_OK);
    CHECK_EQ_INT(record.type, HEX_RECORD_EXTENDED_LINEAR);

    /* Every following data record now lands in flash, not at page zero. */
    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x10000000u);

    CHECK_EQ_INT(feed(":10010000000102030405060708090A0B0C0D0E0F77", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x10000100u);
}

TEST(without_an_extension_record_addresses_stay_in_the_first_64k)
{
    /* Guards against the base leaking in from somewhere: a fresh parser must
       start at zero. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x00000000u);
}

TEST(a_later_extension_record_replaces_the_earlier_base)
{
    /* An image spanning more than 64 KiB emits a new type 04 for each block.
       Adding rather than replacing would send the second block astray. */
    setup();
    hex_record_t record;

    feed(":020000041000EA", &record);
    feed(":04000000DEADBEEFC4", &record);
    CHECK_EQ_U32(record.address, 0x10000000u);

    CHECK_EQ_INT(feed(":020000041001E9", &record), HEX_OK);
    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x10010000u);
}

TEST(reset_clears_the_address_base)
{
    setup();
    hex_record_t record;

    feed(":020000041000EA", &record);
    hex_parser_reset(&parser);

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x00000000u);
    CHECK(!hex_parser_is_complete(&parser));
}

TEST(an_extended_segment_record_scales_by_sixteen)
{
    /* The 8086 form: the value is a paragraph number. Unlikely from an ARM
       toolchain, but wrong is worse than unsupported. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":020000021230BA", &record), HEX_OK);
    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x12300u);
}

TEST(a_start_address_record_is_recorded_not_applied)
{
    /* Type 05 gives the entry point. It must not disturb the address base. */
    setup();
    hex_record_t record;

    feed(":020000041000EA", &record);
    CHECK_EQ_INT(feed(":0400000510000101E5", &record), HEX_OK);
    CHECK_EQ_INT(record.type, HEX_RECORD_START_LINEAR);
    CHECK(parser.have_start_address);
    CHECK_EQ_U32(parser.start_address, 0x10000101u);

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x10000000u);
}

/* ---------------------------------------------------------------------------
 * Malformed input
 *
 * All of it arrives over a serial link, so every one of these is something
 * that will actually happen.
 * -------------------------------------------------------------------------*/

TEST(a_line_without_a_start_code_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed("04000000DEADBEEFC4", &record), HEX_ERR_NO_START_CODE);
    CHECK_EQ_INT(feed("", &record), HEX_ERR_NO_START_CODE);
    CHECK_EQ_INT(feed("   \r\n", &record), HEX_ERR_NO_START_CODE);
    CHECK_EQ_INT(feed("# a comment", &record), HEX_ERR_NO_START_CODE);
}

TEST(a_corrupted_checksum_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEFC5", &record), HEX_ERR_BAD_CHECKSUM);
}

TEST(a_corrupted_data_byte_is_caught_by_the_checksum)
{
    /* The failure the checksum exists for: one byte mangled in transit. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEEC4", &record), HEX_ERR_BAD_CHECKSUM);
}

TEST(a_non_hex_character_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEZC4", &record), HEX_ERR_BAD_CHARACTER);
}

TEST(a_line_shorter_than_its_byte_count_is_rejected)
{
    /* A truncated line whose remaining bytes happen to sum correctly must not
       be accepted with a short read of its data. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADC4", &record), HEX_ERR_BAD_LENGTH);
}

TEST(a_line_longer_than_its_byte_count_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4FF", &record), HEX_ERR_BAD_LENGTH);
}

TEST(an_odd_number_of_digits_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":04000000DEADBEEFC", &record), HEX_ERR_BAD_LENGTH);
}

TEST(a_line_too_short_to_be_a_record_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":", &record), HEX_ERR_BAD_LENGTH);
    CHECK_EQ_INT(feed(":00", &record), HEX_ERR_BAD_LENGTH);
    CHECK_EQ_INT(feed(":000000", &record), HEX_ERR_BAD_LENGTH);
}

TEST(an_unknown_record_type_is_rejected)
{
    /* Type 0x06 does not exist. Built with a correct checksum so that only the
       type check can reject it. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":00000006FA", &record), HEX_ERR_UNKNOWN_TYPE);
}

TEST(an_extension_record_of_the_wrong_size_is_rejected)
{
    /* Right shape and right checksum, but a type 04 must carry exactly two
       bytes. Accepting it would set a nonsense address base. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":03000004100000E9", &record), HEX_ERR_MALFORMED_RECORD);
}

TEST(an_end_of_file_record_carrying_data_is_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":0100000100FE", &record), HEX_ERR_MALFORMED_RECORD);
}

TEST(records_after_the_end_of_file_are_rejected)
{
    /* Trailing rubbish on the link would otherwise be written into the image
       after the transfer was supposedly complete. */
    setup();
    hex_record_t record;

    CHECK_EQ_INT(feed(":00000001FF", &record), HEX_OK);
    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_ERR_AFTER_EOF);
    CHECK_EQ_INT(feed(":00000001FF", &record), HEX_ERR_AFTER_EOF);
}

TEST(a_rejected_record_leaves_the_parser_usable)
{
    /* One bad line on a noisy link must not poison the rest of the transfer. */
    setup();
    hex_record_t record;

    feed(":020000041000EA", &record);
    CHECK_EQ_INT(feed(":04000000DEADBEEFC5", &record), HEX_ERR_BAD_CHECKSUM);

    CHECK_EQ_INT(feed(":04000000DEADBEEFC4", &record), HEX_OK);
    CHECK_EQ_U32(record.address, 0x10000000u);
}

TEST(null_arguments_are_rejected)
{
    setup();
    hex_record_t record;

    CHECK_EQ_INT(hex_parser_feed(NULL, ":00000001FF", &record), HEX_ERR_INVALID_ARG);
    CHECK_EQ_INT(hex_parser_feed(&parser, NULL, &record), HEX_ERR_INVALID_ARG);
    CHECK_EQ_INT(hex_parser_feed(&parser, ":00000001FF", NULL), HEX_ERR_INVALID_ARG);
}

/* ---------------------------------------------------------------------------
 * A whole file
 * -------------------------------------------------------------------------*/

TEST(a_complete_image_reassembles_in_order)
{
    /* What the parser is actually for: a short image at the RP2040 flash base,
       fed line by line as it would arrive. */
    static const char *const file[] = {
        ":020000041000EA",
        ":10000000000102030405060708090A0B0C0D0E0F78",
        ":10001000101112131415161718191A1B1C1D1E1F68",
        ":00000001FF",
    };

    setup();

    uint8_t image[32];
    size_t total = 0;
    uint32_t first_address = 0;

    for (unsigned i = 0; i < count_of_(file); i++) {
        hex_record_t record;
        const hex_result_t result = feed(file[i], &record);
        if (result != HEX_OK) {
            printf("    line %u (%s): %s\n", i, file[i], hex_result_name(result));
            CHECK(false);
            return;
        }
        if (record.type != HEX_RECORD_DATA) {
            continue;
        }
        if (total == 0) {
            first_address = record.address;
        }
        CHECK(total + record.length <= sizeof(image));
        memcpy(&image[total], record.data, record.length);
        total += record.length;
    }

    CHECK(hex_parser_is_complete(&parser));
    CHECK_EQ_U32(first_address, 0x10000000u);
    CHECK_EQ_INT(total, 32);
    for (unsigned i = 0; i < 32; i++) {
        if (image[i] != i) {
            CHECK_EQ_INT(image[i], i);
            return;
        }
    }
}

TEST_MAIN(
    RUN(a_data_record_decodes);
    RUN(the_textbook_record_decodes);
    RUN(an_end_of_file_record_decodes);
    RUN(lowercase_hex_digits_are_accepted);
    RUN(surrounding_whitespace_is_tolerated);

    RUN(an_extended_linear_record_sets_the_upper_address_half);
    RUN(without_an_extension_record_addresses_stay_in_the_first_64k);
    RUN(a_later_extension_record_replaces_the_earlier_base);
    RUN(reset_clears_the_address_base);
    RUN(an_extended_segment_record_scales_by_sixteen);
    RUN(a_start_address_record_is_recorded_not_applied);

    RUN(a_line_without_a_start_code_is_rejected);
    RUN(a_corrupted_checksum_is_rejected);
    RUN(a_corrupted_data_byte_is_caught_by_the_checksum);
    RUN(a_non_hex_character_is_rejected);
    RUN(a_line_shorter_than_its_byte_count_is_rejected);
    RUN(a_line_longer_than_its_byte_count_is_rejected);
    RUN(an_odd_number_of_digits_is_rejected);
    RUN(a_line_too_short_to_be_a_record_is_rejected);
    RUN(an_unknown_record_type_is_rejected);
    RUN(an_extension_record_of_the_wrong_size_is_rejected);
    RUN(an_end_of_file_record_carrying_data_is_rejected);
    RUN(records_after_the_end_of_file_are_rejected);
    RUN(a_rejected_record_leaves_the_parser_usable);
    RUN(null_arguments_are_rejected);

    RUN(a_complete_image_reassembles_in_order);
)
