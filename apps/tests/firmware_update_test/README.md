# firmware_update_test

Runs the pure update-path logic on the target.

## Why, when the host tests cover the same ground

They cover it far more thoroughly — but the host and the target are not the
same machine, and two things have to agree between them:

* **The image header is written to flash byte for byte.** Its size and field
  offsets are a contract between whatever builds an image and the bootloader
  that reads one. A compiler that padded the struct differently would make an
  image written by one unreadable to the other.
* **CRC values must match.** An image checksummed by a build tool on a PC has
  to validate on the microcontroller, or no update ever completes.

It also keeps `crc`, `ring_buffer`, `hex_parser` and `firmware_update` in the
ARM build. They are pure, so nothing else links them yet; without this
application CI would only ever compile them for the host.

## Required hardware

**Nothing.** Any RP2040 or RP2350 board, no wiring.

## Running

```bash
make BOARD=pico2 APP=tests/firmware_update_test
make BOARD=pico2 APP=tests/firmware_update_test flash
picocom -b 115200 /dev/ttyACM0
```

## Expected result

```text
firmware_update_test  board=pico2

--- pass 0 ---
  [pass] header size on target                28 bytes, expected 28
  [pass] header field offsets                 match the host layout
  [pass] crc32 check value                    0xCBF43926, expected 0xCBF43926
  [pass] crc16 check value                    0x29B1, expected 0x29B1
  [pass] crc32 incremental equals one-shot    512 bytes in 37s
  [pass] hex image reassembles                32 bytes at 0x10000000
  [pass] hex rejects a bad checksum           bad checksum
  ...
  17 checks, 0 failed
```

All checks should pass. The counts are printed so a failure is obvious without
reading every line.

## Interpreting failures

| Symptom | Meaning |
|---------|---------|
| header size or offsets wrong | the target packs the struct differently from the host — images would not be portable between the two, and the format needs explicit packing |
| CRC check value wrong | the target's `crc` disagrees with every other tool; no externally built image would ever validate |
| incremental disagrees with one-shot | an image checksummed in chunks as it arrives would not match the same image read back whole |
| hex image does not reassemble | address-extension handling differs on the target |
| ring buffer fails after wraps | index arithmetic differs on a 32-bit target |

## What this does not cover

Anything touching flash. Nothing here erases or programs, and there is no
bootloader yet — this is the logic that a bootloader will be built on, checked
in isolation first.
