/*
 * Host-side tests for the persistent firmware image header.
 *
 * This format is expensive to get wrong and cheap to test: every one of these
 * cases is something that happens when an update is interrupted, and none of
 * them needs a flash chip to reproduce.
 */

#include <string.h>

#include "test.h"

#include "crc.h"
#include "firmware_image.h"

static firmware_image_header_t make_header(uint32_t size, uint32_t payload_crc,
                                           uint32_t build_id)
{
    firmware_image_header_t header;
    CHECK_EQ_INT(firmware_image_header_init(&header, size, payload_crc,
                                            0x10000000u, build_id),
                 FIRMWARE_IMAGE_OK);
    return header;
}

/* ---------------------------------------------------------------------------
 * Layout
 * -------------------------------------------------------------------------*/

TEST(the_header_layout_is_fixed)
{
    /*
     * The header is written to flash by one program and read by another, so
     * its size and field offsets are a contract. A compiler that padded it
     * differently would make an image written in one session unreadable in the
     * next.
     */
    CHECK_EQ_INT(sizeof(firmware_image_header_t), 28);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, magic), 0);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, header_version), 4);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, header_size), 6);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, payload_size), 8);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, payload_crc32), 12);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, load_address), 16);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, build_id), 20);
    CHECK_EQ_INT(offsetof(firmware_image_header_t, header_crc32), 24);
}

TEST(a_freshly_built_header_validates)
{
    const firmware_image_header_t header = make_header(1024, 0xDEADBEEFu, 7);

    CHECK_EQ_INT(firmware_image_header_validate(&header), FIRMWARE_IMAGE_OK);
    CHECK(firmware_image_header_is_valid(&header));
    CHECK_EQ_U32(header.magic, FIRMWARE_IMAGE_MAGIC);
    CHECK_EQ_U32(header.payload_size, 1024u);
    CHECK_EQ_U32(header.build_id, 7u);
}

TEST(the_header_has_no_padding)
{
    /*
     * Every byte of the struct belongs to a field, so writing it to flash
     * cannot capture uninitialised padding. This is what makes the byte-wise
     * comparison below meaningful; if a future field breaks it, the memset in
     * firmware_image_header_init() becomes load-bearing rather than defensive.
     */
    const size_t fields =
        sizeof(((firmware_image_header_t *)0)->magic) +
        sizeof(((firmware_image_header_t *)0)->header_version) +
        sizeof(((firmware_image_header_t *)0)->header_size) +
        sizeof(((firmware_image_header_t *)0)->payload_size) +
        sizeof(((firmware_image_header_t *)0)->payload_crc32) +
        sizeof(((firmware_image_header_t *)0)->load_address) +
        sizeof(((firmware_image_header_t *)0)->build_id) +
        sizeof(((firmware_image_header_t *)0)->header_crc32);

    CHECK_EQ_INT(sizeof(firmware_image_header_t), fields);
}

TEST(building_a_header_leaves_no_undefined_bytes)
{
    /*
     * Two headers built from the same values must be byte-identical, or the
     * same image would checksum differently on two runs. Filling the structs
     * with different rubbish first would expose any field init() forgets.
     */
    firmware_image_header_t a;
    firmware_image_header_t b;
    memset(&a, 0x00, sizeof(a));
    memset(&b, 0xA5, sizeof(b));

    firmware_image_header_init(&a, 2048, 0x12345678u, 0x10000000u, 42);
    firmware_image_header_init(&b, 2048, 0x12345678u, 0x10000000u, 42);

    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

/* ---------------------------------------------------------------------------
 * Rejecting what an interrupted update leaves behind
 * -------------------------------------------------------------------------*/

TEST(erased_flash_is_not_a_header)
{
    /* An erased manifest sector reads as all ones on a board that has never
       staged an update. */
    firmware_image_header_t header;
    memset(&header, 0xFF, sizeof(header));

    CHECK_EQ_INT(firmware_image_header_validate(&header), FIRMWARE_IMAGE_ERR_BAD_MAGIC);
    CHECK(!firmware_image_header_is_valid(&header));
}

TEST(zeroed_flash_is_not_a_header)
{
    /* And this is what a sector looks like when it has been written but not
       yet filled in. */
    firmware_image_header_t header;
    memset(&header, 0x00, sizeof(header));

    CHECK_EQ_INT(firmware_image_header_validate(&header), FIRMWARE_IMAGE_ERR_BAD_MAGIC);
}

TEST(a_damaged_header_is_rejected_field_by_field)
{
    /*
     * Flip every bit of every field except the CRC itself, and check that the
     * header CRC catches all of them. This is what protects against a header
     * half-written when the power failed.
     */
    const firmware_image_header_t good = make_header(4096, 0xCAFEBABEu, 3);

    const size_t covered = sizeof(good) - sizeof(uint32_t);
    for (size_t byte = 0; byte < covered; byte++) {
        for (unsigned bit = 0; bit < 8; bit++) {
            firmware_image_header_t damaged = good;
            ((uint8_t *)&damaged)[byte] ^= (uint8_t)(1u << bit);

            if (firmware_image_header_is_valid(&damaged)) {
                printf("    bit %u of byte %u went undetected\n", bit,
                       (unsigned)byte);
                CHECK(false);
                return;
            }
        }
    }
}

TEST(a_damaged_crc_field_is_rejected)
{
    firmware_image_header_t header = make_header(4096, 0xCAFEBABEu, 3);
    header.header_crc32 ^= 1u;

    CHECK_EQ_INT(firmware_image_header_validate(&header),
                 FIRMWARE_IMAGE_ERR_BAD_HEADER_CRC);
}

TEST(an_empty_or_absurd_payload_size_is_rejected)
{
    firmware_image_header_t header;

    CHECK_EQ_INT(firmware_image_header_init(&header, 0, 0, 0x10000000u, 1),
                 FIRMWARE_IMAGE_ERR_BAD_SIZE);
    CHECK_EQ_INT(firmware_image_header_init(&header, FIRMWARE_IMAGE_MAX_PAYLOAD + 1,
                                            0, 0x10000000u, 1),
                 FIRMWARE_IMAGE_ERR_BAD_SIZE);

    /* And a header claiming one, however well formed otherwise, is rejected on
       the way back in. */
    header = make_header(1024, 0, 1);
    header.payload_size = 0;
    header.header_crc32 = firmware_image_header_crc(&header);
    CHECK_EQ_INT(firmware_image_header_validate(&header), FIRMWARE_IMAGE_ERR_BAD_SIZE);
}

TEST(a_header_from_a_future_format_is_rejected)
{
    /* Better to refuse than to misread a layout we do not know. */
    firmware_image_header_t header = make_header(1024, 0, 1);
    header.header_version = FIRMWARE_IMAGE_HEADER_VERSION + 1;
    header.header_crc32 = firmware_image_header_crc(&header);

    CHECK_EQ_INT(firmware_image_header_validate(&header),
                 FIRMWARE_IMAGE_ERR_BAD_VERSION);
}

TEST(a_header_declaring_the_wrong_size_is_rejected)
{
    firmware_image_header_t header = make_header(1024, 0, 1);
    header.header_size = 32;
    header.header_crc32 = firmware_image_header_crc(&header);

    CHECK_EQ_INT(firmware_image_header_validate(&header),
                 FIRMWARE_IMAGE_ERR_BAD_VERSION);
}

TEST(null_is_not_a_header)
{
    CHECK_EQ_INT(firmware_image_header_validate(NULL), FIRMWARE_IMAGE_ERR_INVALID_ARG);
    CHECK(!firmware_image_header_is_valid(NULL));
}

/* ---------------------------------------------------------------------------
 * Payload verification
 * -------------------------------------------------------------------------*/

TEST(a_matching_payload_verifies)
{
    static uint8_t payload[777];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 13u);
    }

    const uint32_t crc = crc32(payload, sizeof(payload));
    const firmware_image_header_t header = make_header(sizeof(payload), crc, 1);

    CHECK_EQ_INT(firmware_image_verify_payload(&header, crc), FIRMWARE_IMAGE_OK);
}

TEST(a_truncated_payload_does_not_verify)
{
    /* The transfer that stopped halfway: the header arrived, the payload did
       not. */
    static uint8_t payload[777];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 13u);
    }

    const firmware_image_header_t header =
        make_header(sizeof(payload), crc32(payload, sizeof(payload)), 1);

    CHECK_EQ_INT(firmware_image_verify_payload(&header, crc32(payload, 700)),
                 FIRMWARE_IMAGE_ERR_BAD_PAYLOAD_CRC);
}

TEST(payload_verification_checks_the_header_first)
{
    /* A damaged header must not be trusted to say what the payload CRC should
       be, so the header error is what comes back. */
    firmware_image_header_t header = make_header(1024, 0x11111111u, 1);
    header.header_crc32 ^= 0xFFu;

    CHECK_EQ_INT(firmware_image_verify_payload(&header, 0x11111111u),
                 FIRMWARE_IMAGE_ERR_BAD_HEADER_CRC);
}

TEST_MAIN(
    RUN(the_header_layout_is_fixed);
    RUN(a_freshly_built_header_validates);
    RUN(the_header_has_no_padding);
    RUN(building_a_header_leaves_no_undefined_bytes);

    RUN(erased_flash_is_not_a_header);
    RUN(zeroed_flash_is_not_a_header);
    RUN(a_damaged_header_is_rejected_field_by_field);
    RUN(a_damaged_crc_field_is_rejected);
    RUN(an_empty_or_absurd_payload_size_is_rejected);
    RUN(a_header_from_a_future_format_is_rejected);
    RUN(a_header_declaring_the_wrong_size_is_rejected);
    RUN(null_is_not_a_header);

    RUN(a_matching_payload_verifies);
    RUN(a_truncated_payload_does_not_verify);
    RUN(payload_verification_checks_the_header_first);

)
