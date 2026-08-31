#include <stddef.h>

#include "flash_layout.h"

#ifndef PICO_FLASH_SIZE_BYTES
/* Only reached in a host build, where there is no board header. The value is
   irrelevant to the tests, which compute layouts explicitly. */
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

flash_layout_result_t flash_layout_compute(uint32_t flash_size, uint32_t data_sectors,
                                           flash_layout_t *layout)
{
    if (layout == NULL) {
        return FLASH_LAYOUT_ERR_INVALID_ARG;
    }
    if (!flash_offset_is_sector_aligned(flash_size)) {
        return FLASH_LAYOUT_ERR_INVALID_ARG;
    }

    const uint32_t total_sectors = flash_size / FLASH_LAYOUT_SECTOR_SIZE;

    /* One sector each for application and staging, plus the manifest, is the
       smallest layout that still means anything. */
    if (data_sectors < FLASH_LAYOUT_MANIFEST_SECTORS + 1u ||
        total_sectors < data_sectors + 2u) {
        return FLASH_LAYOUT_ERR_TOO_SMALL;
    }

    const uint32_t image_sectors = (total_sectors - data_sectors) / 2u;

    /*
     * An odd sector count leaves one over. It goes to the data region rather
     * than to either image, so that application and staging stay exactly the
     * same size — installing is a copy between them, and an image that fits in
     * one must fit in the other.
     */
    const uint32_t image_size = image_sectors * FLASH_LAYOUT_SECTOR_SIZE;

    layout->application.offset = 0;
    layout->application.size = image_size;

    layout->staging.offset = image_size;
    layout->staging.size = image_size;

    /* The manifest takes the front of the reserved tail, so that erasing it —
       which happens on every transfer — cannot disturb config or logs. */
    layout->manifest.offset = 2u * image_size;
    layout->manifest.size = FLASH_LAYOUT_MANIFEST_SECTORS * FLASH_LAYOUT_SECTOR_SIZE;

    layout->data.offset = layout->manifest.offset + layout->manifest.size;
    layout->data.size = flash_size - layout->data.offset;

    return FLASH_LAYOUT_OK;
}

const flash_layout_t *flash_layout_get(void)
{
    static flash_layout_t layout;
    static bool computed;

    if (!computed) {
        /* A failure here would mean the build is configured for a chip too
           small to hold two images; leaving the layout zeroed makes every
           bounds check reject, which is the safe outcome. */
        if (flash_layout_compute(PICO_FLASH_SIZE_BYTES, FLASH_LAYOUT_DATA_SECTORS,
                                 &layout) != FLASH_LAYOUT_OK) {
            layout = (flash_layout_t){ 0 };
        }
        computed = true;
    }
    return &layout;
}

bool flash_region_contains(const flash_region_t *region, uint32_t offset, uint32_t size)
{
    if (region == NULL || region->size == 0) {
        return false;
    }
    if (size == 0) {
        return offset <= region->size;
    }
    /* Checked this way round so a wrapping offset + size cannot pass. */
    if (offset > region->size || size > region->size - offset) {
        return false;
    }
    return true;
}

flash_layout_result_t flash_region_check_erase(const flash_region_t *region,
                                               uint32_t offset, uint32_t size)
{
    if (region == NULL) {
        return FLASH_LAYOUT_ERR_INVALID_ARG;
    }
    if (!flash_region_contains(region, offset, size)) {
        return FLASH_LAYOUT_ERR_OUT_OF_RANGE;
    }

    /*
     * Both ends must be on a sector boundary. Erasing has no partial form —
     * the hardware clears the whole sector — so a request covering part of a
     * sector would silently destroy the rest of it.
     *
     * The region's own start is checked too: a region that did not begin on a
     * boundary would make every offset inside it misaligned in the chip.
     */
    if (!flash_offset_is_sector_aligned(region->offset) ||
        !flash_offset_is_sector_aligned(offset) ||
        !flash_offset_is_sector_aligned(size)) {
        return FLASH_LAYOUT_ERR_UNALIGNED;
    }

    return FLASH_LAYOUT_OK;
}

flash_layout_result_t flash_region_check_program(const flash_region_t *region,
                                                 uint32_t offset, uint32_t size)
{
    if (region == NULL) {
        return FLASH_LAYOUT_ERR_INVALID_ARG;
    }
    if (!flash_region_contains(region, offset, size)) {
        return FLASH_LAYOUT_ERR_OUT_OF_RANGE;
    }
    if (!flash_offset_is_page_aligned(region->offset) ||
        !flash_offset_is_page_aligned(offset) ||
        !flash_offset_is_page_aligned(size)) {
        return FLASH_LAYOUT_ERR_UNALIGNED;
    }

    return FLASH_LAYOUT_OK;
}

const char *flash_layout_result_name(flash_layout_result_t result)
{
    switch (result) {
        case FLASH_LAYOUT_OK:                return "ok";
        case FLASH_LAYOUT_ERR_INVALID_ARG:   return "invalid argument";
        case FLASH_LAYOUT_ERR_TOO_SMALL:     return "flash too small to divide";
        case FLASH_LAYOUT_ERR_UNALIGNED:     return "not on a sector or page boundary";
        case FLASH_LAYOUT_ERR_OUT_OF_RANGE:  return "outside the region";
        default:                             return "unknown";
    }
}
