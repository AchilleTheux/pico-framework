/*
 * firmware_apply - install a staged image over the running one.
 *
 * ============================== READ THIS =============================
 *
 * This is the only operation in the framework that can leave a board
 * unbootable. It erases and rewrites the region it was itself running from,
 * and there is a window of a few seconds during which the board holds neither
 * a complete old firmware nor a complete new one. Losing power in that window
 * leaves nothing to boot.
 *
 * BOOTSEL remains the recovery path. The button and USB are unaffected by
 * anything here, so a board interrupted mid-install can always be reflashed
 * over USB. What is lost is the ability to recover *over the serial link*,
 * which is the whole point of the feature — so on a deployed board the window
 * is a real risk and not a theoretical one.
 *
 * It is compiled out unless FIRMWARE_SERVICE_ENABLE_APPLY is set, and the
 * command that reaches it refuses to run on an image that has not been
 * verified against the sender's checksum.
 *
 * ======================================================================
 *
 * How it works, and why it is shaped this way:
 *
 *   The copy runs from RAM. Once the first sector of the application region is
 *   erased, any call into a function living in flash would jump into erased
 *   space. The routine is therefore __no_inline_not_in_flash_func, calls only
 *   other RAM-resident functions, and touches hardware registers directly
 *   rather than through the SDK helpers that live in flash.
 *
 *   The SDK's flash_range_erase() and flash_range_program() are themselves
 *   RAM-resident and handle exiting and re-entering XIP around each call, on
 *   both RP2040 and RP2350. Using them rather than driving the flash chip over
 *   raw SPI — which is what the firmware this was modelled on did — keeps the
 *   routine portable across the two architectures and removes the need to
 *   reimplement the chip's command set.
 *
 *   Between those calls XIP is back on, so the staged image is read through
 *   the normal memory window a sector at a time.
 *
 *   A watchdog is armed before the copy and petted between sectors, by writing
 *   its load register directly. It is a backstop for the copy hanging, not the
 *   normal path: the routine resets the chip deliberately when it finishes.
 */

#ifndef PICO_FRAMEWORK_FIRMWARE_APPLY_H
#define PICO_FRAMEWORK_FIRMWARE_APPLY_H

#include <stdint.h>

#include "flash_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How long the copy may stall before the watchdog gives up and resets. Petted
 * between sectors, so this bounds one sector's erase and program rather than
 * the whole image — a 4 KiB sector erase is tens of milliseconds typically and
 * a few hundred at worst.
 */
#ifndef FIRMWARE_APPLY_WATCHDOG_MS
#define FIRMWARE_APPLY_WATCHDOG_MS 4000u
#endif

/*
 * Copy `size` bytes from `source` over `destination`, then reset the chip.
 *
 * Does not return: it resets on success, and the watchdog resets it on a
 * stall. Interrupts are disabled for the duration, so nothing else on this
 * core runs; the caller is responsible for the other core being parked.
 *
 * `size` is rounded up to a whole number of sectors. The caller must have
 * checked that it fits in `destination` and that the image has been verified;
 * this routine is past the point where anything can be reported.
 */
void firmware_apply(const flash_region_t *source, const flash_region_t *destination,
                    uint32_t size) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FIRMWARE_APPLY_H */
