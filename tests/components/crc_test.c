/*
 * Host-side tests for the CRC implementations.
 *
 * The values below are the published check values for these standard
 * parameterisations, not output captured from this code. That is the whole
 * point: a CRC is only useful if a value computed here matches one computed by
 * whatever writes the firmware image on the other end of the wire, so the
 * tests have to pin the implementation to the specification.
 */

#include <string.h>

#include "test.h"

#include "crc.h"

/* The canonical CRC check string. */
static const char CHECK[] = "123456789";

TEST(crc32_matches_the_published_check_value)
{
    /* CRC-32/ISO-HDLC check value: 0xCBF43926. Same as `crc32` on the command
       line, zip, and PNG. */
    CHECK_EQ_U32(crc32(CHECK, strlen(CHECK)), 0xCBF43926u);
}

TEST(crc32_of_nothing_is_zero)
{
    CHECK_EQ_U32(crc32("", 0), 0u);
    CHECK_EQ_U32(crc32(NULL, 0), 0u);
}

TEST(crc32_matches_known_short_inputs)
{
    CHECK_EQ_U32(crc32("a", 1), 0xE8B7BE43u);
    CHECK_EQ_U32(crc32("abc", 3), 0x352441C2u);
    CHECK_EQ_U32(crc32("message digest", 14), 0x20159D7Fu);
}

TEST(crc32_incremental_matches_one_shot)
{
    /* The property the firmware update depends on: an image checksummed in
       arbitrary chunks as it arrives must equal the same image checksummed
       whole when read back from flash. */
    static uint8_t payload[997];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 31u + 7u);
    }

    const uint32_t whole = crc32(payload, sizeof(payload));

    static const size_t chunk_sizes[] = { 1, 2, 3, 16, 64, 255, 256, 512 };
    for (unsigned c = 0; c < count_of_(chunk_sizes); c++) {
        const size_t chunk = chunk_sizes[c];
        uint32_t state = crc32_begin();
        for (size_t offset = 0; offset < sizeof(payload); offset += chunk) {
            size_t remaining = sizeof(payload) - offset;
            if (remaining > chunk) {
                remaining = chunk;
            }
            state = crc32_update(state, &payload[offset], remaining);
        }
        if (crc32_end(state) != whole) {
            printf("    chunk size %u disagreed with the one-shot value\n",
                   (unsigned)chunk);
            CHECK_EQ_U32(crc32_end(state), whole);
            return;
        }
    }
}

TEST(crc32_detects_a_single_bit_flip)
{
    /* The failure a firmware image checksum exists to catch. */
    static uint8_t image[512];
    for (size_t i = 0; i < sizeof(image); i++) {
        image[i] = (uint8_t)i;
    }

    const uint32_t good = crc32(image, sizeof(image));

    for (size_t byte = 0; byte < sizeof(image); byte += 37) {
        for (unsigned bit = 0; bit < 8; bit++) {
            image[byte] ^= (uint8_t)(1u << bit);
            const uint32_t corrupted = crc32(image, sizeof(image));
            image[byte] ^= (uint8_t)(1u << bit);

            if (corrupted == good) {
                printf("    bit %u of byte %u went undetected\n", bit,
                       (unsigned)byte);
                CHECK(false);
                return;
            }
        }
    }
}

TEST(crc32_detects_truncation)
{
    /* An update cut short by a dropped connection must not validate. */
    static uint8_t image[256];
    for (size_t i = 0; i < sizeof(image); i++) {
        image[i] = (uint8_t)(i ^ 0x5A);
    }

    const uint32_t full = crc32(image, sizeof(image));
    for (size_t len = 1; len < sizeof(image); len++) {
        if (crc32(image, len) == full) {
            printf("    a %u-byte prefix has the same CRC as the whole\n",
                   (unsigned)len);
            CHECK(false);
            return;
        }
    }
}

TEST(crc32_distinguishes_leading_zeroes)
{
    /* The reason CRC-32 starts at 0xFFFFFFFF rather than 0: with a zero init,
       any number of leading zero bytes gives the same result, and a partly
       erased flash sector reads as zeroes. */
    static const uint8_t one_zero[1] = { 0 };
    static const uint8_t two_zeroes[2] = { 0, 0 };
    static const uint8_t ten_zeroes[10] = { 0 };

    CHECK(crc32(one_zero, sizeof(one_zero)) != crc32(two_zeroes, sizeof(two_zeroes)));
    CHECK(crc32(two_zeroes, sizeof(two_zeroes)) != crc32(ten_zeroes, sizeof(ten_zeroes)));
}

TEST(crc32_of_erased_flash_is_not_zero)
{
    /* Erased flash reads as 0xFF. A checksum of it must not collide with the
       "no image" value that a blank header would hold. */
    static uint8_t erased[256];
    memset(erased, 0xFF, sizeof(erased));

    const uint32_t value = crc32(erased, sizeof(erased));
    CHECK(value != 0u);
    CHECK(value != 0xFFFFFFFFu);
}

/* ---------------------------------------------------------------------------
 * CRC-16/CCITT-FALSE
 * -------------------------------------------------------------------------*/

TEST(crc16_matches_the_published_check_value)
{
    /* CRC-16/CCITT-FALSE check value: 0x29B1. */
    CHECK_EQ_INT(crc16_ccitt(CHECK, strlen(CHECK)), 0x29B1);
}

TEST(crc16_of_nothing_is_the_initial_value)
{
    /* No final xor in this parameterisation, so an empty message yields the
       init value rather than 0. */
    CHECK_EQ_INT(crc16_ccitt("", 0), 0xFFFF);
}

TEST(crc16_matches_known_short_inputs)
{
    CHECK_EQ_INT(crc16_ccitt("A", 1), 0xB915);
    CHECK_EQ_INT(crc16_ccitt("AB", 2), 0x4B74);
}

TEST(crc16_incremental_matches_one_shot)
{
    static uint8_t payload[301];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 17u + 3u);
    }

    const uint16_t whole = crc16_ccitt(payload, sizeof(payload));

    uint16_t state = crc16_ccitt_begin();
    for (size_t offset = 0; offset < sizeof(payload); offset += 7) {
        size_t remaining = sizeof(payload) - offset;
        if (remaining > 7) {
            remaining = 7;
        }
        state = crc16_ccitt_update(state, &payload[offset], remaining);
    }

    CHECK_EQ_INT(crc16_ccitt_end(state), whole);
}

TEST(crc16_detects_a_single_bit_flip)
{
    static uint8_t frame[64];
    for (size_t i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(i * 3u);
    }

    const uint16_t good = crc16_ccitt(frame, sizeof(frame));

    for (size_t byte = 0; byte < sizeof(frame); byte++) {
        frame[byte] ^= 0x01u;
        const uint16_t corrupted = crc16_ccitt(frame, sizeof(frame));
        frame[byte] ^= 0x01u;

        if (corrupted == good) {
            printf("    a flipped bit in byte %u went undetected\n",
                   (unsigned)byte);
            CHECK(false);
            return;
        }
    }
}

TEST(the_two_algorithms_are_independent)
{
    /* Guards against one being wired to the other's polynomial. */
    CHECK((uint16_t)crc32(CHECK, strlen(CHECK)) != crc16_ccitt(CHECK, strlen(CHECK)));
}

TEST_MAIN(
    RUN(crc32_matches_the_published_check_value);
    RUN(crc32_of_nothing_is_zero);
    RUN(crc32_matches_known_short_inputs);
    RUN(crc32_incremental_matches_one_shot);
    RUN(crc32_detects_a_single_bit_flip);
    RUN(crc32_detects_truncation);
    RUN(crc32_distinguishes_leading_zeroes);
    RUN(crc32_of_erased_flash_is_not_zero);

    RUN(crc16_matches_the_published_check_value);
    RUN(crc16_of_nothing_is_the_initial_value);
    RUN(crc16_matches_known_short_inputs);
    RUN(crc16_incremental_matches_one_shot);
    RUN(crc16_detects_a_single_bit_flip);
    RUN(the_two_algorithms_are_independent);
)
