#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

#include "crc.h"

#include "flash_storage.h"

/* flash_layout.h declares these itself so it can stay free of the SDK. If the
   SDK ever disagrees, every bound in this component would be computed against
   the wrong geometry. */
_Static_assert(FLASH_LAYOUT_SECTOR_SIZE == FLASH_SECTOR_SIZE,
               "flash_layout sector size disagrees with the SDK");
_Static_assert(FLASH_LAYOUT_PAGE_SIZE == FLASH_PAGE_SIZE,
               "flash_layout page size disagrees with the SDK");

/* Arguments for the operations run under flash_safe_execute(), which takes a
   single void pointer. */
typedef struct {
    uint32_t absolute_offset;
    const uint8_t *data;
    uint32_t size;
} flash_operation_t;

static void do_erase(void *param)
{
    const flash_operation_t *op = (const flash_operation_t *)param;
    flash_range_erase(op->absolute_offset, op->size);
}

static void do_program(void *param)
{
    const flash_operation_t *op = (const flash_operation_t *)param;
    flash_range_program(op->absolute_offset, op->data, op->size);
}

bool flash_storage_holds_running_code(const flash_region_t *region)
{
    if (region == NULL || region->size == 0) {
        return false;
    }

    /*
     * The address of a function in this translation unit, turned into an
     * offset within the chip. Comparing that against the region is exact and
     * needs no assumption about where the image was linked — it works equally
     * for an application at offset 0 and for a future bootloader below one.
     */
    const uintptr_t here = (uintptr_t)(const void *)&flash_storage_holds_running_code;
    if (here < XIP_BASE) {
        return false; /* executing from RAM, so no flash region holds us */
    }

    const uint32_t offset = (uint32_t)(here - XIP_BASE);
    return offset >= region->offset && offset < region->offset + region->size;
}

static flash_storage_result_t translate(flash_layout_result_t result)
{
    switch (result) {
        case FLASH_LAYOUT_OK:               return FLASH_STORAGE_OK;
        case FLASH_LAYOUT_ERR_UNALIGNED:    return FLASH_STORAGE_ERR_UNALIGNED;
        case FLASH_LAYOUT_ERR_OUT_OF_RANGE: return FLASH_STORAGE_ERR_OUT_OF_RANGE;
        default:                            return FLASH_STORAGE_ERR_INVALID_ARG;
    }
}

/* Checks common to erase and program: the region is real, the span is inside
   it, and we are not about to destroy the code doing the destroying. */
static flash_storage_result_t precheck(const flash_region_t *region)
{
    if (region == NULL || region->size == 0) {
        return FLASH_STORAGE_ERR_INVALID_ARG;
    }
    if (flash_storage_holds_running_code(region)) {
        return FLASH_STORAGE_ERR_RUNNING_FROM_REGION;
    }
    return FLASH_STORAGE_OK;
}

flash_storage_result_t flash_storage_erase(const flash_region_t *region,
                                           uint32_t offset, uint32_t size)
{
    const flash_storage_result_t checked = precheck(region);
    if (checked != FLASH_STORAGE_OK) {
        return checked;
    }

    const flash_storage_result_t bounds =
        translate(flash_region_check_erase(region, offset, size));
    if (bounds != FLASH_STORAGE_OK) {
        return bounds;
    }
    if (size == 0) {
        return FLASH_STORAGE_OK;
    }

    flash_operation_t op = {
        .absolute_offset = flash_region_absolute(region, offset),
        .data = NULL,
        .size = size,
    };

    /* flash_safe_execute holds off the other core and interrupts. It refuses
       rather than proceeding if it cannot, which is the behaviour we want:
       a half-held-off erase is worse than no erase. */
    if (flash_safe_execute(do_erase, &op, FLASH_STORAGE_LOCKOUT_TIMEOUT_MS) != PICO_OK) {
        return FLASH_STORAGE_ERR_NOT_SAFE;
    }

    return FLASH_STORAGE_OK;
}

flash_storage_result_t flash_storage_program(const flash_region_t *region,
                                             uint32_t offset,
                                             const void *data, uint32_t size)
{
    const flash_storage_result_t checked = precheck(region);
    if (checked != FLASH_STORAGE_OK) {
        return checked;
    }
    if (data == NULL && size > 0) {
        return FLASH_STORAGE_ERR_INVALID_ARG;
    }

    const flash_storage_result_t bounds =
        translate(flash_region_check_program(region, offset, size));
    if (bounds != FLASH_STORAGE_OK) {
        return bounds;
    }
    if (size == 0) {
        return FLASH_STORAGE_OK;
    }

    flash_operation_t op = {
        .absolute_offset = flash_region_absolute(region, offset),
        .data = (const uint8_t *)data,
        .size = size,
    };

    if (flash_safe_execute(do_program, &op, FLASH_STORAGE_LOCKOUT_TIMEOUT_MS) != PICO_OK) {
        return FLASH_STORAGE_ERR_NOT_SAFE;
    }

    return FLASH_STORAGE_OK;
}

flash_storage_result_t flash_storage_program_verified(const flash_region_t *region,
                                                      uint32_t offset,
                                                      const void *data, uint32_t size)
{
    const flash_storage_result_t written =
        flash_storage_program(region, offset, data, size);
    if (written != FLASH_STORAGE_OK) {
        return written;
    }

    const uint8_t *stored = flash_storage_data(region, offset, size);
    if (stored == NULL) {
        return FLASH_STORAGE_ERR_OUT_OF_RANGE;
    }
    if (memcmp(stored, data, size) != 0) {
        return FLASH_STORAGE_ERR_VERIFY_FAILED;
    }

    return FLASH_STORAGE_OK;
}

const uint8_t *flash_storage_data(const flash_region_t *region, uint32_t offset,
                                  uint32_t size)
{
    if (region == NULL || !flash_region_contains(region, offset, size)) {
        return NULL;
    }
    return (const uint8_t *)(XIP_BASE + flash_region_absolute(region, offset));
}

bool flash_storage_is_erased(const flash_region_t *region, uint32_t offset,
                             uint32_t size)
{
    const uint8_t *bytes = flash_storage_data(region, offset, size);
    if (bytes == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (bytes[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

uint32_t flash_storage_crc32(const flash_region_t *region, uint32_t offset,
                             uint32_t size)
{
    const uint8_t *bytes = flash_storage_data(region, offset, size);
    if (bytes == NULL) {
        return 0;
    }

    /* Straight over the XIP window: flash is memory as far as reading goes, so
       nothing has to be copied into RAM first. */
    return crc32(bytes, size);
}

const char *flash_storage_result_name(flash_storage_result_t result)
{
    switch (result) {
        case FLASH_STORAGE_OK:                     return "ok";
        case FLASH_STORAGE_ERR_INVALID_ARG:        return "invalid argument";
        case FLASH_STORAGE_ERR_OUT_OF_RANGE:       return "outside the region";
        case FLASH_STORAGE_ERR_UNALIGNED:          return "not on a sector or page boundary";
        case FLASH_STORAGE_ERR_RUNNING_FROM_REGION: return "would erase the running code";
        case FLASH_STORAGE_ERR_NOT_SAFE:           return "could not hold off the other core";
        case FLASH_STORAGE_ERR_VERIFY_FAILED:      return "read back differs from what was written";
        default:                                   return "unknown";
    }
}
