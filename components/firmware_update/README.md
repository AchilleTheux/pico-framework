# firmware_update

The on-flash description of a firmware image, staged reception over a CLI, and
the opt-in routine that installs a verified image in place.

The format and receive state machine are the parts of an over-the-wire update
that are most expensive to get wrong and cheapest to test, so their policy is
kept independent of the Pico SDK and verified on the host.

## The problem it solves

An update over a serial link can be interrupted at any point: the cable is
pulled, the battery sags, the sender crashes halfway. Every field here exists so
that a half-written image is recognisably half-written rather than something the
board tries to run.

## Image header

28 bytes, written to flash verbatim, so the layout is a contract between the
service that verifies an image and the later session that recovers it:

| Offset | Field | Purpose |
|--------|-------|---------|
| 0 | `magic` | `"PFW1"`. Neither erased flash (`0xFFFFFFFF`) nor blank flash (`0x00000000`) can be mistaken for it |
| 4 | `header_version` | refuse a layout we do not know rather than misread it |
| 6 | `header_size` | |
| 8 | `payload_size` | |
| 12 | `payload_crc32` | over the payload, from the `crc` component |
| 16 | `load_address` | |
| 20 | `build_id` | identifies this build; see below |
| 24 | `header_crc32` | over everything above |

Two checksums, not one, because they are checked at different times: the header
is 28 bytes and is validated constantly, while the payload is tens of kilobytes
and is streamed. A torn header can be told from a good one without reading the
payload at all.

## The manifest

The header is written to its own flash sector when a transfer has been
verified, and read back at startup. That is what lets an image be uploaded, the
board power-cycled, and the image installed afterwards without sending it
again — which matters, because on a link slow enough to need this feature a
resend is minutes.

Recovering it checks **both** halves: that the header is valid, and that
re-checksumming the staged bytes still matches what it claims. Trusting the
header alone would let a manifest survive an interrupted transfer that had
already started erasing staging underneath it.

`fwbegin` clears the manifest before touching anything else, so the two can
never be left disagreeing. `fwstatus` says when a verified image came from
before the last reboot rather than from this session.

The manifest has a sector to itself precisely so that rewriting it on every
transfer cannot disturb config or logs, which sit after it in the layout.

There was also a `firmware_image_decide_boot()` here, written for a two-slot
bootloader that chose between images at startup. The design that shipped
installs in place instead and never makes a decision at boot, so it has been
removed rather than kept as something that looks live and is not.

## Status

The image format, receiver, flash-backed service, persistent manifest, and
RAM-resident in-place installer are implemented. The installer is compiled out
unless `FIRMWARE_SERVICE_ENABLE_APPLY=ON`; a default build can stage and verify
an image but cannot overwrite the running application.

Host tests cover the pure policy and CI checks the host sender's predicted
checksum against the device implementation over real linked HEX images. The
flash-backed receive and install path has not yet run on hardware.

## Testing

`make test` covers the header layout and checksums, Intel HEX decoding, staging
address bounds, page assembly, out-of-order records, erased gaps, failure
states, and the flash layout. The cases that earn their place are the ones an
interrupted or reordered transfer actually produces. Manifest recovery and the
CLI service are target-only and remain part of the pending hardware test.

One known gap, stated rather than hidden: removing the `memset()` in
`firmware_image_header_init()` does not fail any test, because the struct
happens to have no padding today. A test pins that property, and the comment on
the `memset` explains that it guards a future field rather than a present bug.


## Receiving an image

`firmware_receive` sits between `hex_parser`, which turns a line into an
address and some bytes, and `flash_storage`, which writes whole 256-byte pages.
It maps an absolute address to a staging offset, gathers bytes into a page,
flushes when the next record lands elsewhere, and refuses anything outside the
region.

Pages go to a caller-supplied writer rather than to flash directly, which is
what makes the whole of it testable on the host. The cases that matter are not
convenient to provoke on hardware:

| Case | Why it matters |
|------|----------------|
| a record straddling a page boundary | both halves must land in the right places |
| records arriving out of order | nothing requires a HEX file to ascend, so the page must flush on a backward jump too |
| gaps in the image | a linker emits nothing between sections. Those bytes must stay `0xFF`; writing zeroes would be **permanent**, since programming can only clear bits |
| two records in one page | re-flushing per record would program the same page twice without an erase between |
| one bad record | the transfer must stay failed, or an image with a hole in it still reaches the end-of-file record and looks ready to install |

## The checksum

Computed by reading flash back after the transfer, not by accumulating as
records arrive. Records may arrive out of order and images have gaps, so a
running total would depend on arrival order — and reading back also confirms
what actually landed rather than what was sent.

The host predicts the same value. That agreement is the property the whole
feature rests on, so `scripts/check-hex-agreement.sh` runs both
implementations over real built images on every CI run: the device's own
`firmware_receive` compiled for the host, against `scripts/serial_update.py`.

## Installing

`firmware_apply` copies staging over the application region and resets. It is
compiled out unless `FIRMWARE_SERVICE_ENABLE_APPLY` is set.

The routine runs from RAM: once the first sector of the application region is
erased, a call into anything living in flash would jump into erased space. That
it really is in RAM is checked in the binary rather than assumed —
`firmware_apply` links at `0x2000xxxx`, and the only calls it makes after the
first erase are to `flash_range_erase` and `flash_range_program`, which the SDK
also places in RAM.

Using those rather than driving the flash chip over raw SPI — which is what the
firmware this was modelled on did — keeps it working on both RP2040 and RP2350,
whose flash interfaces differ, and avoids reimplementing the chip's command
set. They handle leaving and re-entering XIP around each call, so the staged
image can be read through the normal memory window between them.

**It costs 4 KiB of RAM**, for the sector buffer, in any build that includes it.

## What has not been verified

Everything below has been written and builds clean, and none of it has run on
hardware. Worth a review before it does:

* **`firmware_apply` itself.** The RAM residency and the call targets are
  checked in the linked binary, but the sequence has never executed. The
  specific things to look at are whether the watchdog petting is frequent
  enough for a slow sector erase, and whether reading the staged image through
  XIP between `flash_range_program` calls is as safe as it looks.
* **The reset.** It writes `watchdog_hw->load = 0` to expire the watchdog
  immediately, relying on the `PSM_WDSEL` bits `watchdog_enable()` set earlier.
  A deliberate reset through the SDK would be clearer but lives in flash.
* **The other core.** Nothing parks it; the header says the caller must. A
  single-core application is fine, and every application here is single-core.
* **Erase timing.** `fwbegin` erases synchronously, and a whole-region erase on
  a 2 MiB part takes around eleven seconds. The host allows thirty.
