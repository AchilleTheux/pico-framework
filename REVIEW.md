# Project Review Findings

Reviewed `main` at commit `6b47334`.

## Findings

### P1 — Reserved flash regions are not enforced by the linker

`flash_storage_holds_running_code()` checks only the address of one function
(`components/flash_storage/flash_storage.c:51`). The generated linker scripts
still permit applications to occupy the full 2 or 4 MiB of physical flash,
despite the logical application/staging/data split defined in
`components/flash_storage/flash_layout.c:40`.

A sufficiently large firmware can therefore extend into the staging region.
`fwbegin` would consider that region safe and could erase live code or read-only
data. Enforce the application boundary in the linker (or with a link-time
assertion based on complete image-end symbols) rather than inferring safety
from one function address.

### P1 — MCP2515 acceptance filters use the wrong bank layout

`configure_filters()` in `components/mcp2515/mcp2515.c:201` models the six
filters as two groups of three. The MCP2515 actually assigns RXF0-RXF1 to
RXM0/RXB0 and RXF2-RXF5 to RXM1/RXB1.

With one to three configured filters, the implementation places RXB1 in
accept-all mode, so rejected frames can still enter through that buffer. With
larger lists, RXF2 is validated and configured against the wrong mask. The
bank assignment and duplication behavior for partially populated banks should
be redesigned around the hardware's 2+4 layout.

Reference: Microchip, *MCP2515 Family Data Sheet*, section 4.5:
https://www.microchip.com/content/dam/mchp/documents/APID/ProductDocuments/DataSheets/MCP2515-Family-Data-Sheet-DS20001801K.pdf

### P2 — Bluetooth retry handling reorders buffered output

`bt_stream_on_can_send()` removes a packet from the outgoing ring buffer before
calling the sender (`components/bluetooth/bt_stream.c:105`). If the sender
accepts only part of that packet, the rejected suffix is appended behind bytes
that were already queued.

For example, partially accepting `abcd` from `abcdefghij` changes the queued
remainder from `cdefghij` to `efghijcd`. A refused or partial RFCOMM send can
therefore scramble console output. Copy without consuming first, then discard
only the prefix actually accepted by the sender.

### P2 — MCP2515 reset delay is too short for advertised 8 MHz controllers

`mcp2515_reset()` in `components/mcp2515/mcp2515.c:84` always waits 10 us after
issuing an SPI reset. The MCP2515 requires 128 oscillator cycles before another
SPI operation. That is 16 us at 8 MHz, an oscillator frequency used by the
component's own README example.

An early status read can consequently report a working controller as absent.
Compute the delay from `oscillator_hz`, rounding upward and allowing an
appropriate margin.

Reference: Microchip, *MCP2515 Family Data Sheet*, section 8.1:
https://www.microchip.com/content/dam/mchp/documents/APID/ProductDocuments/DataSheets/MCP2515-Family-Data-Sheet-DS20001801K.pdf

### P2 — A zero-length I2C write/read leaves the bus without a STOP

`i2c_device_write_read()` performs its write with `nostop=true`, then returns
success when `rx == NULL` or `rx_len == 0`
(`components/i2c_device/i2c_device.c:109`). The Pico SDK contract says that
`nostop=true` retains control of the bus so that the next transfer begins with
a repeated START.

If no read follows, the transaction is never terminated normally. Reject an
empty read or issue the write with `nostop=false` in that case.

### P2 — I2C presence checks are not non-destructive as claimed

`i2c_device_present()` describes a zero-length probe but actually performs a
one-byte read (`components/i2c_device/i2c_device.c:127`). Probing or scanning
can therefore consume FIFO data, advance a device's register pointer, or clear
a read-sensitive status register.

The API should document that side effect or require/select a probe operation
appropriate for the particular device instead of promising a non-destructive
generic check.

## Resolution

All six findings are addressed on `fix/review-findings`:

- Flash layout arithmetic is shared with the build, and linker overrides keep
  every flash-writing application inside the application region. This covers
  `simple_robot`, both firmware-update benches, and the persistent-config
  benches (`config_test` and `wifi_test`).
- MCP2515 filters now use the hardware's 2+4 bank layout, populate both receive
  buffers for short filter lists, and are covered by host tests.
- Bluetooth output is peeked before sending and only the accepted prefix is
  discarded, preserving FIFO order after a partial send.
- The MCP2515 reset wait is derived from the configured oscillator frequency.
- A write/read operation without a read now emits a normal STOP, while a NULL
  receive buffer with a non-zero length is rejected.
- The I2C presence API and documentation now state that probing consumes one
  byte and can have device-visible side effects.

## Verification

- All 21 AddressSanitizer/UBSan host tests pass. Leak detection was disabled in
  the managed review runner because LeakSanitizer cannot run under its tracing;
  AddressSanitizer and UBSan remained enabled.
- Quick CI passes for RP2040 and RP2350 with warnings treated as errors.
- The newly protected `simple_robot`, `config_test`, and `wifi_test` targets
  link successfully, as does the 4 MiB RP2350 firmware-update target.
- The original missing-runtime condition is fixed: the system now has the
  matching `libasan` and `libubsan` packages installed.
