/*
 * flash_layout - how the flash chip is divided up, and the arithmetic for
 * staying inside those divisions.
 *
 * Separated from the code that actually erases and programs because it is pure
 * integer work and because it is the part that must be right: every bound
 * check that stops an update writing over something it should not is in here,
 * and all of it is unit-tested on the host.
 *
 * The default division follows the shape the reference firmware used, sized
 * from the chip rather than hard-coded:
 *
 *     +------------------+ 0
 *     |   application    |   the running firmware
 *     +------------------+
 *     |     staging      |   an image received but not yet installed
 *     +------------------+
 *     |     manifest     |   one sector: what is staged, and whether it was
 *     |                  |   verified. Survives a reboot, so an image can be
 *     |                  |   uploaded and installed later.
 *     +------------------+
 *     |      data        |   config and logs, kept clear of everything above
 *     +------------------+ end of flash
 *
 * Application and staging are the same size, because installing is a copy from
 * one to the other and an image that fits in staging must fit in the
 * application region too.
 */

#ifndef PICO_FRAMEWORK_FLASH_LAYOUT_H
#define PICO_FRAMEWORK_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RP2040 and RP2350 both use 4 KiB erase sectors and 256-byte program pages.
 * Declared here rather than taken from the SDK so this file stays free of it;
 * flash_storage.c asserts that these match the SDK's own values.
 */
#define FLASH_LAYOUT_SECTOR_SIZE 4096u
#define FLASH_LAYOUT_PAGE_SIZE 256u

/*
 * Sectors reserved at the end of flash for data that must survive an update.
 * 32 sectors is 128 KiB, which is generous for a config file and a log and
 * costs little on a 2 MiB part.
 */
#ifndef FLASH_LAYOUT_DATA_SECTORS
#define FLASH_LAYOUT_DATA_SECTORS 32u
#endif

/* A span of flash, as an offset from the start of the chip — never an XIP
   address, so that nothing here can be dereferenced by accident. */
typedef struct {
    uint32_t offset;
    uint32_t size;
} flash_region_t;

/* One sector is enough for the manifest and is the smallest thing that can be
   erased independently, which is what it needs. */
#define FLASH_LAYOUT_MANIFEST_SECTORS 1u

typedef struct {
    flash_region_t application;
    flash_region_t staging;
    flash_region_t manifest;
    flash_region_t data;
} flash_layout_t;

typedef enum {
    FLASH_LAYOUT_OK = 0,
    FLASH_LAYOUT_ERR_INVALID_ARG,
    FLASH_LAYOUT_ERR_TOO_SMALL,      /* not enough flash to divide up */
    FLASH_LAYOUT_ERR_UNALIGNED,      /* not on a sector boundary */
    FLASH_LAYOUT_ERR_OUT_OF_RANGE,   /* outside the region */
} flash_layout_result_t;

/*
 * Divide a chip of `flash_size` bytes, reserving `data_sectors` at the end and
 * splitting what remains equally between the application and staging. The
 * first sector of the reserved tail becomes the manifest, so `data_sectors`
 * must leave at least one more.
 *
 * Fails rather than producing a degenerate layout when the chip is too small
 * to hold a data region plus one sector each of application and staging.
 */
flash_layout_result_t flash_layout_compute(uint32_t flash_size, uint32_t data_sectors,
                                           flash_layout_t *layout);

/* The layout for this build, from PICO_FLASH_SIZE_BYTES and the settings
   above. Computed once; the same object every call. */
const flash_layout_t *flash_layout_get(void);

/* ---------------------------------------------------------------------------
 * Bounds
 *
 * Offsets passed to these are relative to the region, not to the chip, so a
 * caller that only knows its own region cannot express an address outside it.
 * -------------------------------------------------------------------------*/

static inline uint32_t flash_region_sector_count(const flash_region_t *region)
{
    return region->size / FLASH_LAYOUT_SECTOR_SIZE;
}

static inline bool flash_offset_is_sector_aligned(uint32_t offset)
{
    return (offset % FLASH_LAYOUT_SECTOR_SIZE) == 0;
}

static inline bool flash_offset_is_page_aligned(uint32_t offset)
{
    return (offset % FLASH_LAYOUT_PAGE_SIZE) == 0;
}

/* True when [offset, offset + size) lies wholly inside the region. Overflow of
   offset + size counts as outside rather than wrapping. */
bool flash_region_contains(const flash_region_t *region, uint32_t offset, uint32_t size);

/*
 * Check a span that is about to be erased: inside the region, and both ends on
 * a sector boundary. An erase is the operation with no partial form — the
 * hardware clears a whole sector — so a misaligned request would destroy data
 * outside what the caller asked for.
 */
flash_layout_result_t flash_region_check_erase(const flash_region_t *region,
                                               uint32_t offset, uint32_t size);

/* Check a span about to be programmed: inside the region, and page-aligned. */
flash_layout_result_t flash_region_check_program(const flash_region_t *region,
                                                 uint32_t offset, uint32_t size);

/* Absolute offset within the chip of a position inside a region. */
static inline uint32_t flash_region_absolute(const flash_region_t *region, uint32_t offset)
{
    return region->offset + offset;
}

/* Round up to a whole number of sectors; used to size an erase for a payload
   that does not end on a boundary. */
static inline uint32_t flash_round_up_to_sector(uint32_t size)
{
    return (size + FLASH_LAYOUT_SECTOR_SIZE - 1u) & ~(FLASH_LAYOUT_SECTOR_SIZE - 1u);
}

const char *flash_layout_result_name(flash_layout_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FLASH_LAYOUT_H */
