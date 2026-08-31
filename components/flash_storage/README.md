# flash_storage

Erasing, programming and reading a bounded region of the flash chip.

A thin layer over the SDK's flash functions. What it adds over calling those
directly is the two things that make a firmware update survivable.

## Bounds

Every offset is relative to a `flash_region_t`, so a caller that knows only its
own region cannot express an address outside it. Alignment is checked before
anything is erased, and the check is strict:

> An erase has no partial form — the hardware clears a whole 4 KiB sector — so
> a request off by one byte destroys 4095 bytes of something else, and does it
> without reporting anything.

The arithmetic lives in `flash_layout.h`, has no SDK dependency, and is
unit-tested on the host. That includes the case where a corrupted length would
make `offset + size` wrap to something small and in-range; the comparison is
arranged so it cannot.

## The running-code guard

An operation on the region holding the currently executing code is refused with
`FLASH_STORAGE_ERR_RUNNING_FROM_REGION`. Erasing the sector you are running
from does not fail cleanly — it hangs the chip.

The check is exact rather than assumed: it takes the address of one of its own
functions, converts it to a flash offset, and asks whether the region contains
it. That works equally for an application at offset 0 and for a future
bootloader sitting below one, with nothing to keep in sync.

The `firmware_update_test` application checks this on real hardware without any
risk, because a refusal is the pass condition.

## Layout

The chip is divided from its own size rather than by hard-coded addresses:

```text
+------------------+ 0
|   application    |  the running firmware
+------------------+
|     staging      |  an image received but not yet installed
+------------------+
|      data        |  config and logs, kept clear of both images
+------------------+ end of flash
```

On the 2 MiB part of the reference board:

| Region | Offset | Size |
|--------|--------|------|
| application | `0x000000` | 960 KiB |
| staging | `0x0F0000` | 960 KiB |
| data | `0x1E0000` | 128 KiB |

Application and staging are deliberately equal: installing is a copy from one
to the other, so an image that fits in staging must fit in the application
region. When the sector count does not halve evenly the spare sector goes to
the data region rather than to either image, so that invariant holds. A host
test checks it across every chip size from 34 sectors to 16 MiB.

`FLASH_LAYOUT_DATA_SECTORS` sets the reserved tail; the rest follows.

## Usage

```c
const flash_layout_t *layout = flash_layout_get();

/* Erase enough sectors for the image, rounded up. */
flash_storage_erase(&layout->staging, 0, flash_round_up_to_sector(image_size));

/* Program page-aligned chunks as they arrive. */
flash_storage_program_verified(&layout->staging, offset, page, sizeof(page));

/* Reading is just memory, through the XIP window. */
const uint8_t *image = flash_storage_data(&layout->staging, 0, image_size);
uint32_t crc = flash_storage_crc32(&layout->staging, 0, image_size);
```

`flash_storage_program()` writes; `flash_storage_program_verified()` writes and
reads back. For a firmware image the read-back is worth its cost, since a bad
write is otherwise discovered by the board failing to boot.

Programming can only clear bits, never set them, so writing over data that was
not erased first silently produces the bitwise AND of the two. The component
does not check for that; erase first.

## Concurrency

Every erase and program runs under the SDK's `flash_safe_execute()`, which
holds off the other core and interrupts. If that guarantee cannot be had the
operation is **refused** rather than attempted — a half-held-off erase is worse
than no erase. That surfaces as `FLASH_STORAGE_ERR_NOT_SAFE`, and on a
multicore application it usually means the other core has not called
`multicore_lockout_victim_init()`.

## Status

Erase, page program, readback verification, and repeated sector use are
hardware-validated indirectly through `persistent_config` on an RP2040-Zero.
The bounds arithmetic and layout remain host-tested. The staging-region
`write_flash` profile and update workload have not run on hardware, nor has the
multicore lockout path.

## Testing

* Host: `make test` covers the division of the chip and every bounds check.
* Hardware: `make APP=tests/firmware_update_test` for the read-only and guard
  checks; `PROFILE=write_flash` additionally erases and programs a staging
  sector, which is destructive to staging but never to the running firmware.
