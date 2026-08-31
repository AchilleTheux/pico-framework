/*
 * Host-side tests for assembling an incoming image into flash pages.
 *
 * Pages go to an array here rather than to flash, which is the point: the
 * cases that matter are records straddling a page boundary, records arriving
 * out of order, and images with gaps in them, and none of those is convenient
 * to provoke on hardware.
 */

#include <string.h>

#include "test.h"

#include "firmware_receive.h"

#define BASE 0x10000000u
#define PAGE FIRMWARE_RECEIVE_PAGE_SIZE
#define CAPACITY (16u * 1024u)

/* Stand-in for the staging region, pre-filled the way erased flash reads. */
static struct {
    uint8_t data[CAPACITY];
    uint32_t writes;
    uint32_t fail_after;   /* 0 = never fail */
} g_flash;

static bool write_page(void *ctx, uint32_t offset, const uint8_t *page, uint32_t size)
{
    (void)ctx;

    if (g_flash.fail_after != 0 && g_flash.writes >= g_flash.fail_after) {
        return false;
    }
    if (offset + size > sizeof(g_flash.data)) {
        return false;
    }

    memcpy(&g_flash.data[offset], page, size);
    g_flash.writes++;
    return true;
}

static firmware_receive_t rx;

static void setup(void)
{
    memset(&g_flash, 0, sizeof(g_flash));
    memset(g_flash.data, 0xFF, sizeof(g_flash.data));

    const firmware_receive_config_t config = {
        .base_address = BASE,
        .capacity = CAPACITY,
        .write_page = write_page,
        .write_ctx = NULL,
    };
    CHECK_EQ_INT(firmware_receive_begin(&rx, &config), FIRMWARE_RECEIVE_OK);
}

/* Build an Intel HEX record, so tests read as the data they mean. */
static const char *record(uint8_t type, uint16_t address, const uint8_t *data,
                          uint8_t length)
{
    static char line[600];
    static const char digits[] = "0123456789ABCDEF";

    uint8_t body[260];
    size_t n = 0;
    body[n++] = length;
    body[n++] = (uint8_t)(address >> 8);
    body[n++] = (uint8_t)address;
    body[n++] = type;
    for (uint8_t i = 0; i < length; i++) {
        body[n++] = data[i];
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum = (uint8_t)(sum + body[i]);
    }
    body[n++] = (uint8_t)(-(int)sum);

    size_t at = 0;
    line[at++] = ':';
    for (size_t i = 0; i < n; i++) {
        line[at++] = digits[body[i] >> 4];
        line[at++] = digits[body[i] & 0x0F];
    }
    line[at] = '\0';
    return line;
}

/* Point the parser at 0x1000xxxx, as a real image does. */
static void set_base(void)
{
    static const uint8_t upper[2] = { 0x10, 0x00 };
    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x04, 0, upper, 2)),
                 FIRMWARE_RECEIVE_OK);
}

static const char *eof_record(void)
{
    return record(0x01, 0, NULL, 0);
}

/* ---------------------------------------------------------------------------
 * The straightforward case
 * -------------------------------------------------------------------------*/

TEST(a_small_image_lands_where_it_should)
{
    setup();
    set_base();

    uint8_t payload[16];
    for (unsigned i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i + 1u);
    }

    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_OK);
    CHECK_EQ_INT(firmware_receive_line(&rx, eof_record()), FIRMWARE_RECEIVE_COMPLETE);

    CHECK(firmware_receive_is_complete(&rx));
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 16u);
    CHECK(memcmp(g_flash.data, payload, sizeof(payload)) == 0);
}

TEST(the_rest_of_a_part_filled_page_is_left_erased)
{
    /*
     * The detail that would be permanent if wrong. Programming can only clear
     * bits, so writing zeroes into the unused tail of a page would make those
     * bytes unwritable later without another erase.
     */
    setup();
    set_base();

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload)));
    firmware_receive_line(&rx, eof_record());

    for (unsigned i = sizeof(payload); i < PAGE; i++) {
        if (g_flash.data[i] != 0xFF) {
            printf("    byte %u of the page is 0x%02X, not erased\n", i,
                   g_flash.data[i]);
            CHECK(false);
            return;
        }
    }
}

TEST(an_image_of_exactly_one_page_writes_one_page)
{
    setup();
    set_base();

    uint8_t payload[32];
    memset(payload, 0xA5, sizeof(payload));

    for (unsigned i = 0; i < PAGE / sizeof(payload); i++) {
        CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, (uint16_t)(i * sizeof(payload)),
                                                       payload, sizeof(payload))),
                     FIRMWARE_RECEIVE_OK);
    }
    firmware_receive_line(&rx, eof_record());

    CHECK_EQ_U32(firmware_receive_image_size(&rx), PAGE);
    CHECK_EQ_INT(g_flash.writes, 1);
}

/* ---------------------------------------------------------------------------
 * Page boundaries
 * -------------------------------------------------------------------------*/

TEST(a_record_straddling_a_page_boundary_is_split_correctly)
{
    /* A 32-byte record at offset 240 crosses into the second page. Both halves
       must land, in the right places. */
    setup();
    set_base();

    uint8_t payload[32];
    for (unsigned i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(0x40u + i);
    }

    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 240, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_OK);
    firmware_receive_line(&rx, eof_record());

    CHECK_EQ_U32(firmware_receive_image_size(&rx), 240u + 32u);
    CHECK(memcmp(&g_flash.data[240], payload, sizeof(payload)) == 0);
    CHECK_EQ_INT(g_flash.writes, 2);
}

TEST(a_record_spanning_several_pages_is_split_correctly)
{
    setup();
    set_base();

    /* 255 bytes is the format's maximum, enough to cross two boundaries when
       it starts near the end of a page. */
    uint8_t payload[255];
    for (unsigned i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 3u + 1u);
    }

    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 250, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_OK);
    firmware_receive_line(&rx, eof_record());

    CHECK(memcmp(&g_flash.data[250], payload, sizeof(payload)) == 0);
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 250u + 255u);
}

TEST(a_contiguous_image_of_many_pages_is_intact)
{
    setup();
    set_base();

    static uint8_t expected[2048];
    for (unsigned i = 0; i < sizeof(expected); i++) {
        expected[i] = (uint8_t)(i ^ (i >> 8));
    }

    /* 16 bytes per record, as a linker commonly emits. */
    for (unsigned offset = 0; offset < sizeof(expected); offset += 16) {
        CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, (uint16_t)offset,
                                                       &expected[offset], 16)),
                     FIRMWARE_RECEIVE_OK);
    }
    firmware_receive_line(&rx, eof_record());

    CHECK_EQ_U32(firmware_receive_image_size(&rx), sizeof(expected));
    CHECK(memcmp(g_flash.data, expected, sizeof(expected)) == 0);
    CHECK_EQ_INT(g_flash.writes, sizeof(expected) / PAGE);
}

/* ---------------------------------------------------------------------------
 * Awkward images
 * -------------------------------------------------------------------------*/

TEST(a_gap_in_the_image_is_left_erased)
{
    /* A linker emits nothing for a hole between sections. Those bytes must
       stay erased rather than being written as zero. */
    setup();
    set_base();

    static const uint8_t first[4] = { 1, 2, 3, 4 };
    static const uint8_t second[4] = { 5, 6, 7, 8 };

    firmware_receive_line(&rx, record(0x00, 0, first, sizeof(first)));
    firmware_receive_line(&rx, record(0x00, 1024, second, sizeof(second)));
    firmware_receive_line(&rx, eof_record());

    CHECK(memcmp(&g_flash.data[0], first, sizeof(first)) == 0);
    CHECK(memcmp(&g_flash.data[1024], second, sizeof(second)) == 0);

    for (unsigned i = 4; i < 1024; i++) {
        if (g_flash.data[i] != 0xFF) {
            printf("    byte %u in the gap is 0x%02X\n", i, g_flash.data[i]);
            CHECK(false);
            return;
        }
    }
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 1028u);
}

TEST(records_arriving_out_of_order_still_land_correctly)
{
    /* Nothing requires a HEX file to be in ascending order, and the page
       buffer must be flushed when the address jumps backwards as well as
       forwards. */
    setup();
    set_base();

    static const uint8_t late[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    static const uint8_t early[4] = { 0x11, 0x22, 0x33, 0x44 };

    firmware_receive_line(&rx, record(0x00, 512, late, sizeof(late)));
    firmware_receive_line(&rx, record(0x00, 0, early, sizeof(early)));
    firmware_receive_line(&rx, eof_record());

    CHECK(memcmp(&g_flash.data[0], early, sizeof(early)) == 0);
    CHECK(memcmp(&g_flash.data[512], late, sizeof(late)) == 0);
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 516u);
}

TEST(two_records_in_the_same_page_produce_one_write)
{
    /* Re-flushing the same page for every record would be correct but would
       program each page repeatedly, which flash does not allow without an
       erase in between. */
    setup();
    set_base();

    static const uint8_t a[4] = { 1, 2, 3, 4 };
    static const uint8_t b[4] = { 5, 6, 7, 8 };

    firmware_receive_line(&rx, record(0x00, 0, a, sizeof(a)));
    firmware_receive_line(&rx, record(0x00, 16, b, sizeof(b)));
    firmware_receive_line(&rx, eof_record());

    CHECK_EQ_INT(g_flash.writes, 1);
    CHECK(memcmp(&g_flash.data[0], a, sizeof(a)) == 0);
    CHECK(memcmp(&g_flash.data[16], b, sizeof(b)) == 0);
}

/* ---------------------------------------------------------------------------
 * Refusals
 * -------------------------------------------------------------------------*/

TEST(an_image_larger_than_the_region_is_refused)
{
    setup();

    /* Address the very end of a 16 KiB region and then go past it. */
    static const uint8_t upper[2] = { 0x10, 0x00 };
    firmware_receive_line(&rx, record(0x04, 0, upper, 2));

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, (uint16_t)(CAPACITY - 2),
                                                   payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_ERR_TOO_LARGE);
}

TEST(an_address_below_the_image_base_is_refused)
{
    /* With no extension record the parser reports addresses near zero, far
       below the 0x10000000 base. Accepting them would underflow the offset. */
    setup();

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_ERR_BELOW_BASE);
}

TEST(a_corrupted_record_fails_the_transfer)
{
    setup();
    set_base();

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload)));

    CHECK_EQ_INT(firmware_receive_line(&rx, ":04000000DEADBEEFC5"),
                 FIRMWARE_RECEIVE_ERR_HEX);
}

TEST(a_failed_transfer_can_never_report_complete)
{
    /*
     * The property that matters most here. Carrying on after a bad record
     * would leave an image with a hole in it that still reached the
     * end-of-file record, and so looked ready to install.
     */
    setup();
    set_base();

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload)));
    firmware_receive_line(&rx, ":04000000DEADBEEFC5");

    /* Every later line, including a perfectly good one and the end of file. */
    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 16, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_ERR_ABORTED);
    CHECK_EQ_INT(firmware_receive_line(&rx, eof_record()),
                 FIRMWARE_RECEIVE_ERR_ABORTED);
    CHECK(!firmware_receive_is_complete(&rx));
}

TEST(a_write_failure_fails_the_transfer)
{
    setup();
    set_base();
    g_flash.fail_after = 1;

    uint8_t payload[16];
    memset(payload, 0x5A, sizeof(payload));

    /* The first page flushes as the address crosses into the second, and that
       write succeeds. */
    for (unsigned offset = 0; offset < PAGE + sizeof(payload); offset += sizeof(payload)) {
        CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, (uint16_t)offset, payload,
                                                       sizeof(payload))),
                     FIRMWARE_RECEIVE_OK);
    }
    CHECK_EQ_INT(g_flash.writes, 1);

    /* The second page's write is refused, which must fail the transfer. */
    CHECK_EQ_INT(firmware_receive_line(&rx, eof_record()), FIRMWARE_RECEIVE_ERR_WRITE);
    CHECK(!firmware_receive_is_complete(&rx));

    /* And it stays failed. */
    CHECK_EQ_INT(firmware_receive_line(&rx, eof_record()), FIRMWARE_RECEIVE_ERR_ABORTED);
    CHECK_EQ_INT(firmware_receive_flush(&rx), FIRMWARE_RECEIVE_ERR_ABORTED);
}

TEST(feeding_before_beginning_is_refused)
{
    firmware_receive_t fresh;
    memset(&fresh, 0, sizeof(fresh));

    CHECK_EQ_INT(firmware_receive_line(&fresh, ":00000001FF"),
                 FIRMWARE_RECEIVE_ERR_NOT_STARTED);
    CHECK_EQ_INT(firmware_receive_flush(&fresh), FIRMWARE_RECEIVE_ERR_NOT_STARTED);
}

TEST(beginning_again_discards_the_previous_transfer)
{
    setup();
    set_base();

    static const uint8_t payload[4] = { 1, 2, 3, 4 };
    firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload)));
    firmware_receive_line(&rx, ":04000000DEADBEEFC5");

    setup(); /* begins again */
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 0u);
    CHECK(!firmware_receive_is_complete(&rx));

    set_base();
    CHECK_EQ_INT(firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload))),
                 FIRMWARE_RECEIVE_OK);
}

TEST(begin_rejects_a_configuration_that_cannot_work)
{
    firmware_receive_t fresh;
    firmware_receive_config_t config = {
        .base_address = BASE, .capacity = CAPACITY,
        .write_page = write_page, .write_ctx = NULL,
    };

    CHECK_EQ_INT(firmware_receive_begin(NULL, &config), FIRMWARE_RECEIVE_ERR_INVALID_ARG);
    CHECK_EQ_INT(firmware_receive_begin(&fresh, NULL), FIRMWARE_RECEIVE_ERR_INVALID_ARG);

    config.write_page = NULL;
    CHECK_EQ_INT(firmware_receive_begin(&fresh, &config), FIRMWARE_RECEIVE_ERR_INVALID_ARG);

    config.write_page = write_page;
    config.capacity = 0;
    CHECK_EQ_INT(firmware_receive_begin(&fresh, &config), FIRMWARE_RECEIVE_ERR_INVALID_ARG);
}

/* ---------------------------------------------------------------------------
 * Progress
 * -------------------------------------------------------------------------*/

TEST(image_size_spans_the_gaps_but_bytes_does_not)
{
    /* The two are different numbers and are used for different things: the
       image size is what to checksum and install, bytes is what arrived. */
    setup();
    set_base();

    static const uint8_t four[4] = { 1, 2, 3, 4 };
    firmware_receive_line(&rx, record(0x00, 0, four, sizeof(four)));
    firmware_receive_line(&rx, record(0x00, 1000, four, sizeof(four)));
    firmware_receive_line(&rx, eof_record());

    CHECK_EQ_U32(rx.bytes, 8u);
    CHECK_EQ_U32(firmware_receive_image_size(&rx), 1004u);
}

TEST(an_early_flush_writes_the_part_filled_page)
{
    setup();
    set_base();

    static const uint8_t payload[4] = { 9, 8, 7, 6 };
    firmware_receive_line(&rx, record(0x00, 0, payload, sizeof(payload)));

    CHECK_EQ_INT(g_flash.writes, 0);   /* nothing written yet */
    CHECK_EQ_INT(firmware_receive_flush(&rx), FIRMWARE_RECEIVE_OK);
    CHECK_EQ_INT(g_flash.writes, 1);
    CHECK(memcmp(g_flash.data, payload, sizeof(payload)) == 0);

    /* Flushing again writes nothing more. */
    CHECK_EQ_INT(firmware_receive_flush(&rx), FIRMWARE_RECEIVE_OK);
    CHECK_EQ_INT(g_flash.writes, 1);
}

TEST_MAIN(
    RUN(a_small_image_lands_where_it_should);
    RUN(the_rest_of_a_part_filled_page_is_left_erased);
    RUN(an_image_of_exactly_one_page_writes_one_page);

    RUN(a_record_straddling_a_page_boundary_is_split_correctly);
    RUN(a_record_spanning_several_pages_is_split_correctly);
    RUN(a_contiguous_image_of_many_pages_is_intact);

    RUN(a_gap_in_the_image_is_left_erased);
    RUN(records_arriving_out_of_order_still_land_correctly);
    RUN(two_records_in_the_same_page_produce_one_write);

    RUN(an_image_larger_than_the_region_is_refused);
    RUN(an_address_below_the_image_base_is_refused);
    RUN(a_corrupted_record_fails_the_transfer);
    RUN(a_failed_transfer_can_never_report_complete);
    RUN(a_write_failure_fails_the_transfer);
    RUN(feeding_before_beginning_is_refused);
    RUN(beginning_again_discards_the_previous_transfer);
    RUN(begin_rejects_a_configuration_that_cannot_work);

    RUN(image_size_spans_the_gaps_but_bytes_does_not);
    RUN(an_early_flush_writes_the_part_filled_page);
)
