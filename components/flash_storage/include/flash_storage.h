/*
 * flash_storage - erasing, programming and reading a region of the flash chip.
 *
 * A thin, bounded layer over the SDK's flash functions. What it adds over
 * calling those directly is the two things that make an update survivable:
 *
 *   Bounds   every offset is relative to a flash_region_t, so a caller that
 *            only knows its own region cannot express an address outside it.
 *            Alignment is checked before anything is erased.
 *
 *   A guard  an operation on the region holding the currently executing code
 *            is refused. Erasing the sector you are running from does not
 *            fail, it hangs the chip, and it is an easy mistake to make with
 *            an offset arithmetic slip.
 *
 * Offsets are relative to the region. Sizes for erase must be whole sectors
 * (4 KiB) and for program whole pages (256 bytes), because that is what the
 * hardware does; flash_layout.h has the arithmetic for rounding to them.
 *
 * Not interrupt-safe by itself: the SDK's flash_safe_execute() is used to hold
 * off the other core and interrupts for the duration, and an operation is
 * refused rather than attempted if that guarantee cannot be had.
 */

#ifndef PICO_FRAMEWORK_FLASH_STORAGE_H
#define PICO_FRAMEWORK_FLASH_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How long to wait for the other core to park itself before giving up. */
#ifndef FLASH_STORAGE_LOCKOUT_TIMEOUT_MS
#define FLASH_STORAGE_LOCKOUT_TIMEOUT_MS 1000u
#endif

typedef enum {
    FLASH_STORAGE_OK = 0,
    FLASH_STORAGE_ERR_INVALID_ARG,
    FLASH_STORAGE_ERR_OUT_OF_RANGE,
    FLASH_STORAGE_ERR_UNALIGNED,
    FLASH_STORAGE_ERR_RUNNING_FROM_REGION, /* would erase the code doing it */
    FLASH_STORAGE_ERR_NOT_SAFE,            /* the other core would not park */
    FLASH_STORAGE_ERR_VERIFY_FAILED,       /* what came back is not what went in */
} flash_storage_result_t;

/*
 * Erase whole sectors. Both `offset` and `size` must be sector-aligned; use
 * flash_round_up_to_sector() on a payload length.
 *
 * Erased flash reads back as 0xFF.
 */
flash_storage_result_t flash_storage_erase(const flash_region_t *region,
                                           uint32_t offset, uint32_t size);

/*
 * Program whole pages into already-erased flash.
 *
 * Programming can only clear bits, never set them, so writing over data that
 * was not erased first silently produces the bitwise AND of the two. This does
 * not check for that — erase first.
 */
flash_storage_result_t flash_storage_program(const flash_region_t *region,
                                             uint32_t offset,
                                             const void *data, uint32_t size);

/*
 * Program and then read back to confirm. Costs a comparison over the data and
 * is worth it for a firmware image, where a bad write is otherwise discovered
 * by the board failing to boot.
 */
flash_storage_result_t flash_storage_program_verified(const flash_region_t *region,
                                                      uint32_t offset,
                                                      const void *data, uint32_t size);

/*
 * A pointer to flash contents, through the memory-mapped XIP window. Valid
 * until the region is next erased. NULL if the span is outside the region.
 *
 * Reading needs no special handling — it is just memory — which is why there
 * is no read function taking a buffer.
 */
const uint8_t *flash_storage_data(const flash_region_t *region, uint32_t offset,
                                  uint32_t size);

/* True when every byte of the span reads 0xFF. */
bool flash_storage_is_erased(const flash_region_t *region, uint32_t offset,
                             uint32_t size);

/* CRC-32 over a span of flash, for checking a staged image against its header
   without copying it into RAM. Returns 0 for a span outside the region. */
uint32_t flash_storage_crc32(const flash_region_t *region, uint32_t offset,
                             uint32_t size);

/*
 * True when `region` holds the code currently executing.
 *
 * Exposed because it is worth asserting in an application: it is what stops an
 * offset slip from erasing the running firmware, and a caller that finds it
 * true for the region it meant to write has a bug worth reporting rather than
 * an operation worth retrying.
 */
bool flash_storage_holds_running_code(const flash_region_t *region);

const char *flash_storage_result_name(flash_storage_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FLASH_STORAGE_H */
