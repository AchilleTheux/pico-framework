# serial_update_test

A firmware that can be replaced over its own console — the reference
implementation of updating a board that has no USB connection at all.

The same serial link carries ordinary commands and a new image: records
beginning with `:` go to the updater, everything else is a command.

## Required hardware

Any RP2040 or RP2350 board. For a genuine test of the feature, reach it over a
UART rather than USB — that is the case the feature exists for.

## Two profiles, and the difference matters

| Profile | `fwapply` |
|---------|-----------|
| `default` | **not built.** Can receive and verify an image, but has no way to install one. Safe on any board. |
| `with_apply` | built. Can overwrite the running firmware. |
| `rebuilt` | as `with_apply`, with build stamp `B` instead of `A`, to send as the update |

The install command is compiled out, not merely disabled — the `default` build
contains no `firmware_apply` symbol at all.

## Trying it

```bash
# Something to run, and something to send it.
make BOARD=pico2 APP=tests/serial_update_test PROFILE=with_apply
make BOARD=pico2 APP=tests/serial_update_test PROFILE=with_apply flash
make BOARD=pico2 APP=tests/serial_update_test PROFILE=rebuilt

# Stage and verify, without installing.
make BOARD=pico2 APP=tests/serial_update_test PROFILE=rebuilt \
     flash-serial /dev/ttyACM0

# The same, and install it.
make BOARD=pico2 APP=tests/serial_update_test PROFILE=rebuilt APPLY=1 \
     flash-serial /dev/ttyACM0
```

The build stamp is how you tell whether it worked: `version` reports `A` before
and `B` after.

## By hand

```text
> version
build   A
> fwbegin 38868          erase enough staging for the image
erasing 40 KiB of staging...
ready, send the hex file
:020000041000EA          (the .hex file, pasted or streamed)
...
:00000001FF
received 38868 bytes in 2436 records
> fwstatus
> fwverify 586EB075      the checksum the sender computed
staged 38868 bytes, crc 0x586EB075
verified
> fwapply
```

`fwbegin` takes an optional size. Omitted, it erases the whole staging region —
about eleven seconds on a 2 MiB part, against under one for a 40 KiB image.

## Before you run fwapply

Read `components/firmware_update/include/firmware_apply.h`. In short:

* The install erases and rewrites the region it is running from. There is a
  window of a few seconds in which the board holds **neither** a complete old
  firmware nor a complete new one.
* **Losing power in that window leaves nothing to boot.**
* **BOOTSEL over USB is the recovery path** and is unaffected — the button and
  the bootrom do not depend on anything here. What is lost is the ability to
  recover *over the serial link*, which is the whole point of the feature. On a
  deployed board that is a real risk, not a theoretical one.
* `fwapply` refuses unless `fwverify` succeeded in this session, so an image is
  never installed without having been checked against the sender's checksum.

## Expected result

| Step | Expect |
|------|--------|
| `version` before | build `A` |
| `fwbegin <size>` | `erasing ... ready, send the hex file` within a second or two |
| streaming | a progress line every 200 records from the host, `. <bytes>` occasionally from the board |
| end of file | `received N bytes in M records`, with N matching what the host printed |
| `fwverify <crc>` | `verified`, with the same CRC the host printed |
| `fwapply` | `installing ...`, then silence, then the board restarts |
| `version` after | build `B` |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| board never answers `fwbegin` | the firmware on it was not built with the update service — only `serial_update_test` has it |
| `no transfer in progress; send fwbegin first` | records arrived outside a transfer, or an earlier record failed and ended it |
| `transfer failed: bad hex record` | a line was corrupted on the link. Lower the baud rate, or check the wiring |
| `transfer failed: image larger than the region` | the image does not fit in staging — check the flash size the image was built for |
| received byte count differs from the host's | lines were dropped; the link cannot keep up. Lower the baud rate |
| `MISMATCH` on verify | what landed is not what was sent. Retry; if it repeats, the link is corrupting data in a way the record checksums are not catching |
| `refusing: run fwverify first` | working as intended |
| board silent after `fwapply` for more than ~10 s | the install stalled. The watchdog should have reset it; if not, recover over BOOTSEL |

## What has and has not been tested

The receive path, the record handling and the checksum agreement between host
and board are covered by host tests and by a CI check that runs both
implementations over real built images.

**No part of this has been run on hardware.** In particular `fwapply` has never
executed. See the final section of `components/firmware_update/README.md` for
what specifically wants reviewing first.
