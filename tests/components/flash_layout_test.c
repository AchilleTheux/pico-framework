/*
 * Host-side tests for the flash division and its bounds checks.
 *
 * Every check here is one that stops a firmware update writing over something
 * it should not. An erase has no partial form — the hardware clears a whole
 * 4 KiB sector — so a request that is off by one byte destroys 4095 bytes of
 * something else, and does it silently.
 */

#include "test.h"

#include "flash_layout.h"

#define SECTOR FLASH_LAYOUT_SECTOR_SIZE
#define MB (1024u * 1024u)

static flash_layout_t layout_for(uint32_t flash_size, uint32_t data_sectors)
{
    flash_layout_t layout;
    CHECK_EQ_INT(flash_layout_compute(flash_size, data_sectors, &layout),
                 FLASH_LAYOUT_OK);
    return layout;
}

/* ---------------------------------------------------------------------------
 * Dividing the chip
 * -------------------------------------------------------------------------*/

TEST(the_regions_do_not_overlap_and_cover_the_chip)
{
    /* The property that matters most: nothing can be in two regions at once,
       and no flash is unaccounted for. */
    static const uint32_t sizes[] = { 2u * MB, 4u * MB, 16u * MB, 512u * 1024u };

    for (unsigned i = 0; i < count_of_(sizes); i++) {
        const flash_layout_t l = layout_for(sizes[i], FLASH_LAYOUT_DATA_SECTORS);

        if (l.application.offset != 0 ||
            l.staging.offset != l.application.offset + l.application.size ||
            l.manifest.offset != l.staging.offset + l.staging.size ||
            l.data.offset != l.manifest.offset + l.manifest.size ||
            l.data.offset + l.data.size != sizes[i]) {
            printf("    %u MiB: app %u+%u staging %u+%u manifest %u+%u data %u+%u\n",
                   sizes[i] / MB, l.application.offset, l.application.size,
                   l.staging.offset, l.staging.size,
                   l.manifest.offset, l.manifest.size, l.data.offset, l.data.size);
            CHECK(false);
            return;
        }
    }
}

TEST(application_and_staging_are_the_same_size)
{
    /* Installing is a copy from staging to the application, so an image that
       fits in one must fit in the other. */
    static const uint32_t sizes[] = { 2u * MB, 4u * MB, 16u * MB };

    for (unsigned i = 0; i < count_of_(sizes); i++) {
        const flash_layout_t l = layout_for(sizes[i], FLASH_LAYOUT_DATA_SECTORS);
        CHECK_EQ_U32(l.application.size, l.staging.size);
    }
}

TEST(an_odd_sector_over_goes_to_data_not_to_an_image)
{
    /* 2 MiB is 512 sectors; reserving 33 leaves 479, which does not halve
       evenly. The spare sector must not make the two images differ. */
    const flash_layout_t l = layout_for(2u * MB, 33u);

    CHECK_EQ_U32(l.application.size, l.staging.size);
    CHECK_EQ_U32(l.data.offset + l.data.size, 2u * MB);
    CHECK(l.manifest.size + l.data.size >= 33u * SECTOR);
}

TEST(every_region_starts_on_a_sector_boundary)
{
    /* A region that did not would make every offset inside it misaligned in
       the chip, and the erase check would reject everything. */
    static const uint32_t sizes[] = { 2u * MB, 4u * MB, 16u * MB, 512u * 1024u };

    for (unsigned i = 0; i < count_of_(sizes); i++) {
        const flash_layout_t l = layout_for(sizes[i], FLASH_LAYOUT_DATA_SECTORS);
        CHECK(flash_offset_is_sector_aligned(l.application.offset));
        CHECK(flash_offset_is_sector_aligned(l.staging.offset));
        CHECK(flash_offset_is_sector_aligned(l.manifest.offset));
        CHECK(flash_offset_is_sector_aligned(l.data.offset));
        CHECK(flash_offset_is_sector_aligned(l.application.size));
        CHECK(flash_offset_is_sector_aligned(l.staging.size));
    }
}

TEST(a_two_megabyte_chip_divides_as_expected)
{
    /* The part on the reference board, worked through explicitly:
       512 sectors, 32 reserved, 480 left, 240 each. */
    const flash_layout_t l = layout_for(2u * MB, 32u);

    CHECK_EQ_U32(l.application.offset, 0u);
    CHECK_EQ_U32(l.application.size, 240u * SECTOR);
    CHECK_EQ_U32(l.staging.offset, 240u * SECTOR);
    CHECK_EQ_U32(l.staging.size, 240u * SECTOR);
    CHECK_EQ_U32(l.manifest.offset, 480u * SECTOR);
    CHECK_EQ_U32(l.manifest.size, SECTOR);
    CHECK_EQ_U32(l.data.offset, 481u * SECTOR);
    CHECK_EQ_U32(l.data.size, 31u * SECTOR);

    /* Nearly a megabyte for firmware; the applications built here are under
       60 KiB, so there is room to grow by a wide margin. */
    CHECK(l.application.size > 900u * 1024u);
}

TEST(a_chip_too_small_to_divide_is_rejected)
{
    flash_layout_t layout;

    /* The reserved tail must hold the manifest and leave something over. */
    CHECK_EQ_INT(flash_layout_compute(2u * MB, 1u, &layout),
                 FLASH_LAYOUT_ERR_TOO_SMALL);
    CHECK_EQ_INT(flash_layout_compute(2u * MB, 2u, &layout), FLASH_LAYOUT_OK);

    /* No room for a data region plus one sector each way. */
    CHECK_EQ_INT(flash_layout_compute(32u * SECTOR, 32u, &layout),
                 FLASH_LAYOUT_ERR_TOO_SMALL);
    CHECK_EQ_INT(flash_layout_compute(33u * SECTOR, 32u, &layout),
                 FLASH_LAYOUT_ERR_TOO_SMALL);

    /* One more sector and it just fits. */
    CHECK_EQ_INT(flash_layout_compute(34u * SECTOR, 32u, &layout), FLASH_LAYOUT_OK);
}

TEST(a_flash_size_that_is_not_whole_sectors_is_rejected)
{
    flash_layout_t layout;
    CHECK_EQ_INT(flash_layout_compute(2u * MB + 1u, 32u, &layout),
                 FLASH_LAYOUT_ERR_INVALID_ARG);
    CHECK_EQ_INT(flash_layout_compute(2u * MB, 32u, NULL),
                 FLASH_LAYOUT_ERR_INVALID_ARG);
}

/* ---------------------------------------------------------------------------
 * Bounds
 * -------------------------------------------------------------------------*/

TEST(a_span_inside_the_region_is_accepted)
{
    const flash_region_t region = { .offset = 100u * SECTOR, .size = 10u * SECTOR };

    CHECK(flash_region_contains(&region, 0, 10u * SECTOR));
    CHECK(flash_region_contains(&region, 0, 1));
    CHECK(flash_region_contains(&region, 10u * SECTOR - 1u, 1));
    CHECK(flash_region_contains(&region, 5u * SECTOR, SECTOR));
}

TEST(a_span_past_the_end_of_the_region_is_rejected)
{
    const flash_region_t region = { .offset = 100u * SECTOR, .size = 10u * SECTOR };

    CHECK(!flash_region_contains(&region, 0, 10u * SECTOR + 1u));
    CHECK(!flash_region_contains(&region, 10u * SECTOR, 1));
    CHECK(!flash_region_contains(&region, 10u * SECTOR - 1u, 2));
}

TEST(a_span_that_would_overflow_is_rejected_not_wrapped)
{
    /*
     * The check that stops a corrupted length turning into a tiny in-range
     * span. offset + size computed naively wraps to something small and would
     * pass; the comparison is arranged so it cannot.
     */
    const flash_region_t region = { .offset = 0, .size = 10u * SECTOR };

    CHECK(!flash_region_contains(&region, SECTOR, 0xFFFFFFFFu));
    CHECK(!flash_region_contains(&region, 0xFFFFFF00u, 0x200u));
    CHECK(!flash_region_contains(&region, 0xFFFFFFFFu, 0xFFFFFFFFu));
}

TEST(nothing_fits_in_an_empty_region)
{
    const flash_region_t empty = { .offset = 0, .size = 0 };

    CHECK(!flash_region_contains(&empty, 0, 1));
    CHECK(!flash_region_contains(NULL, 0, 1));
}

/* ---------------------------------------------------------------------------
 * Erase and program checks
 * -------------------------------------------------------------------------*/

TEST(a_sector_aligned_erase_is_accepted)
{
    const flash_region_t region = { .offset = 240u * SECTOR, .size = 240u * SECTOR };

    CHECK_EQ_INT(flash_region_check_erase(&region, 0, SECTOR), FLASH_LAYOUT_OK);
    CHECK_EQ_INT(flash_region_check_erase(&region, SECTOR, 4u * SECTOR),
                 FLASH_LAYOUT_OK);
    CHECK_EQ_INT(flash_region_check_erase(&region, 0, 240u * SECTOR),
                 FLASH_LAYOUT_OK);
}

TEST(a_misaligned_erase_is_rejected)
{
    /* The one that matters: erasing clears whole sectors, so an offset or a
       length off by a byte destroys the rest of a sector that was not asked
       for, and does it without any error. */
    const flash_region_t region = { .offset = 240u * SECTOR, .size = 240u * SECTOR };

    CHECK_EQ_INT(flash_region_check_erase(&region, 1, SECTOR),
                 FLASH_LAYOUT_ERR_UNALIGNED);
    CHECK_EQ_INT(flash_region_check_erase(&region, 0, SECTOR - 1u),
                 FLASH_LAYOUT_ERR_UNALIGNED);
    CHECK_EQ_INT(flash_region_check_erase(&region, 0, SECTOR + 1u),
                 FLASH_LAYOUT_ERR_UNALIGNED);
    CHECK_EQ_INT(flash_region_check_erase(&region, 0, FLASH_LAYOUT_PAGE_SIZE),
                 FLASH_LAYOUT_ERR_UNALIGNED);
}

TEST(an_erase_past_the_region_is_rejected_before_alignment)
{
    /* Out of range is the more serious complaint of the two, so it is the one
       reported. */
    const flash_region_t region = { .offset = 0, .size = 4u * SECTOR };

    CHECK_EQ_INT(flash_region_check_erase(&region, 0, 5u * SECTOR),
                 FLASH_LAYOUT_ERR_OUT_OF_RANGE);
    CHECK_EQ_INT(flash_region_check_erase(&region, 4u * SECTOR, SECTOR),
                 FLASH_LAYOUT_ERR_OUT_OF_RANGE);
}

TEST(an_erase_in_a_misaligned_region_is_rejected)
{
    /* A region not starting on a sector boundary cannot be erased at all,
       whatever offset is asked for. */
    const flash_region_t bad = { .offset = 100u, .size = 4u * SECTOR };

    CHECK_EQ_INT(flash_region_check_erase(&bad, 0, SECTOR),
                 FLASH_LAYOUT_ERR_UNALIGNED);
}

TEST(a_page_aligned_program_is_accepted)
{
    const flash_region_t region = { .offset = 240u * SECTOR, .size = 240u * SECTOR };

    CHECK_EQ_INT(flash_region_check_program(&region, 0, FLASH_LAYOUT_PAGE_SIZE),
                 FLASH_LAYOUT_OK);
    CHECK_EQ_INT(flash_region_check_program(&region, FLASH_LAYOUT_PAGE_SIZE,
                                            2u * FLASH_LAYOUT_PAGE_SIZE),
                 FLASH_LAYOUT_OK);

    /* A sector is a whole number of pages, so a sector-sized write qualifies. */
    CHECK_EQ_INT(flash_region_check_program(&region, 0, SECTOR), FLASH_LAYOUT_OK);
}

TEST(a_misaligned_program_is_rejected)
{
    const flash_region_t region = { .offset = 240u * SECTOR, .size = 240u * SECTOR };

    CHECK_EQ_INT(flash_region_check_program(&region, 1, FLASH_LAYOUT_PAGE_SIZE),
                 FLASH_LAYOUT_ERR_UNALIGNED);
    CHECK_EQ_INT(flash_region_check_program(&region, 0, 1),
                 FLASH_LAYOUT_ERR_UNALIGNED);
    CHECK_EQ_INT(flash_region_check_program(&region, 0, FLASH_LAYOUT_PAGE_SIZE - 1u),
                 FLASH_LAYOUT_ERR_UNALIGNED);
}

TEST(rounding_up_to_a_sector_is_exact_on_a_boundary)
{
    /* Used to size the erase for a payload that does not end on a boundary.
       Rounding a value that is already aligned must not add a sector, or every
       update would erase one more than it needs. */
    CHECK_EQ_U32(flash_round_up_to_sector(0), 0u);
    CHECK_EQ_U32(flash_round_up_to_sector(SECTOR), SECTOR);
    CHECK_EQ_U32(flash_round_up_to_sector(1), SECTOR);
    CHECK_EQ_U32(flash_round_up_to_sector(SECTOR - 1u), SECTOR);
    CHECK_EQ_U32(flash_round_up_to_sector(SECTOR + 1u), 2u * SECTOR);
    CHECK_EQ_U32(flash_round_up_to_sector(60u * 1024u), 15u * SECTOR);
}

TEST(sector_counts_and_absolute_offsets_are_consistent)
{
    const flash_layout_t l = layout_for(2u * MB, 32u);

    CHECK_EQ_U32(flash_region_sector_count(&l.application), 240u);
    CHECK_EQ_U32(flash_region_sector_count(&l.staging), 240u);
    CHECK_EQ_U32(flash_region_sector_count(&l.manifest), 1u);
    CHECK_EQ_U32(flash_region_sector_count(&l.data), 31u);

    /* An offset inside staging maps to the right place in the chip. */
    CHECK_EQ_U32(flash_region_absolute(&l.staging, 0), 240u * SECTOR);
    CHECK_EQ_U32(flash_region_absolute(&l.staging, SECTOR), 241u * SECTOR);
}

TEST(a_staging_image_always_fits_in_the_application_region)
{
    /* The invariant the install step depends on. Checked across chip sizes so
       an uneven division cannot break it. */
    for (uint32_t sectors = 34; sectors <= 4096; sectors += 7) {
        flash_layout_t l;
        if (flash_layout_compute(sectors * SECTOR, 32u, &l) != FLASH_LAYOUT_OK) {
            continue;
        }
        if (l.staging.size > l.application.size) {
            printf("    %u sectors: staging %u > application %u\n", sectors,
                   l.staging.size, l.application.size);
            CHECK(false);
            return;
        }
    }
}

TEST_MAIN(
    RUN(the_regions_do_not_overlap_and_cover_the_chip);
    RUN(application_and_staging_are_the_same_size);
    RUN(an_odd_sector_over_goes_to_data_not_to_an_image);
    RUN(every_region_starts_on_a_sector_boundary);
    RUN(a_two_megabyte_chip_divides_as_expected);
    RUN(a_chip_too_small_to_divide_is_rejected);
    RUN(a_flash_size_that_is_not_whole_sectors_is_rejected);

    RUN(a_span_inside_the_region_is_accepted);
    RUN(a_span_past_the_end_of_the_region_is_rejected);
    RUN(a_span_that_would_overflow_is_rejected_not_wrapped);
    RUN(nothing_fits_in_an_empty_region);

    RUN(a_sector_aligned_erase_is_accepted);
    RUN(a_misaligned_erase_is_rejected);
    RUN(an_erase_past_the_region_is_rejected_before_alignment);
    RUN(an_erase_in_a_misaligned_region_is_rejected);
    RUN(a_page_aligned_program_is_accepted);
    RUN(a_misaligned_program_is_rejected);
    RUN(rounding_up_to_a_sector_is_exact_on_a_boundary);
    RUN(sector_counts_and_absolute_offsets_are_consistent);
    RUN(a_staging_image_always_fits_in_the_application_region);
)
