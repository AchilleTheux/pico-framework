#include "hardware/flash.h"
#include "hardware/structs/watchdog.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "firmware_apply.h"

/*
 * The RP2040 watchdog decrements twice per microsecond (errata RP2040-E1), so
 * the load value is scaled accordingly. Copied from the SDK's own
 * _watchdog_enable() because watchdog_update() lives in flash and cannot be
 * called once the copy has begun; writing the load register is the whole of
 * what it does.
 */
#if defined(PICO_RP2040) && PICO_RP2040
#define WATCHDOG_TICKS_PER_MS (1000u * 2u)
#else
#define WATCHDOG_TICKS_PER_MS (1000u)
#endif

/* One erase sector, in RAM rather than on the stack: 4 KiB is more than the
   default stack has to spare, and this is the only instance. */
static uint8_t g_sector[FLASH_SECTOR_SIZE];

static uint32_t g_watchdog_load;

/*
 * Reset the chip immediately, without calling into flash.
 *
 * Writing 0 to the watchdog's load register makes it expire at once. The
 * PSM_WDSEL bits that decide how much of the chip a watchdog reset clears were
 * set by watchdog_enable() before the copy started.
 */
static inline void __attribute__((noreturn)) reset_now(void)
{
    watchdog_hw->load = 0;
    while (true) {
        tight_loop_contents();
    }
}

void __no_inline_not_in_flash_func(firmware_apply)(const flash_region_t *source,
                                                   const flash_region_t *destination,
                                                   uint32_t size)
{
    /*
     * Everything that must touch flash-resident code happens here, before the
     * first erase: arming the watchdog, and reading the arguments out of the
     * structures the caller passed. After this point the only calls are to
     * RAM-resident functions and the only reads are from RAM or the XIP window
     * over the *source* region, which is never erased.
     */
    watchdog_enable(FIRMWARE_APPLY_WATCHDOG_MS, false);
    g_watchdog_load = FIRMWARE_APPLY_WATCHDOG_MS * WATCHDOG_TICKS_PER_MS;

    const uint32_t source_offset = source->offset;
    const uint32_t destination_offset = destination->offset;
    const uint32_t sectors =
        (size + FLASH_SECTOR_SIZE - 1u) / FLASH_SECTOR_SIZE;

    /*
     * Nothing else on this core runs from here on; an interrupt handler living
     * in flash would jump into erased space.
     *
     * The *other* core is the caller's problem, and deliberately so: parking
     * it needs multicore_lockout_victim_init() to have been called there, and
     * a blocking lockout attempt against a core that never registered would
     * hang here rather than report anything. A single-core application needs
     * nothing; one using both cores must stop core 1 before calling this.
     */
    (void)save_and_disable_interrupts();

    for (uint32_t sector = 0; sector < sectors; sector++) {
        const uint32_t at = sector * FLASH_SECTOR_SIZE;

        /*
         * Read the staged sector through the XIP window. flash_range_erase()
         * and flash_range_program() restore XIP before returning, so it is
         * available here; the source region is not being written, so what it
         * holds is stable.
         *
         * Copied with an explicit loop rather than memcpy(), which may live in
         * flash.
         */
        const volatile uint8_t *staged =
            (const volatile uint8_t *)(XIP_BASE + source_offset + at);
        for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; i++) {
            g_sector[i] = staged[i];
        }

        /* Both of these are RAM-resident in the SDK and handle leaving and
           re-entering XIP around the operation, on either architecture. */
        flash_range_erase(destination_offset + at, FLASH_SECTOR_SIZE);
        flash_range_program(destination_offset + at, g_sector, FLASH_SECTOR_SIZE);

        /* Pet the watchdog. This is watchdog_update()'s entire body; the
           function itself is in flash and by now unreachable. */
        watchdog_hw->load = g_watchdog_load;
    }

    reset_now();
}
