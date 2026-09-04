# mcp2515_test

Manual hardware test for the SPI `mcp2515` component, on RP2040 and RP2350.

## Required hardware

- An RP2040 or RP2350 board with an MCP2515-compatible SPI CAN controller
  wired to it — for example Waveshare's `rp2350_can` board, which has one
  onboard (an XL2515).
- For `default` (normal mode): a 3.3 V/5 V-compatible CAN transceiver already
  on the controller module, a second active CAN node (a transmitter cannot
  complete successfully unless another node acknowledges it), and 120 ohm
  termination at each end of the bus.
- For `loopback`: nothing beyond the controller itself — no second node, no
  transceiver wiring even needs to be connected. This is the place to start.
- For `monitor`: same as `default` but receive-only.

## Default wiring

Matches the `rp2350_can` board header:

| Signal | GPIO |
|--------|------|
| SCK    | 10   |
| MOSI (SI) | 11 |
| MISO (SO) | 12 |
| CS     | 9    |
| INT    | 8    |

Override with the `MCP2515_TEST_*` CMake cache variables (see
`CMakeLists.txt`) for any other wiring or board.

**Confirm the controller's oscillator frequency before trusting this test on
a shared bus.** `MCP2515_TEST_OSCILLATOR_HZ` defaults to 16 MHz, matching the
`rp2350_can` board's XL2515 crystal (Y1), confirmed against
`docs/RP2350-CAN-Schematic.pdf`. Override it for any other module — a wrong
value mistimes every bit on the bus silently; `mcp2515_bus_init()` only
catches an oscillator/bitrate pair that cannot reach the requested bitrate
*at all*, not one that reaches a different, wrong bitrate exactly.

## Build and run

```bash
make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=loopback
make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=loopback flash
```

Open USB CDC or the default UART at 115200. Loopback needs nothing else
connected and should immediately show `TX`/`RX` pairs for the same frame.

```bash
make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=default flash
```

The second node must use the same 500 kbit/s bitrate. It may transmit any
valid frames. The firmware itself sends, in rotation, a standard data frame
with id `0x123`, an extended data frame with id `0x01ABCDE0`, and an RTR
frame with id `0x321` — so a second board running either this application or
`tests/can_test` is a peer for it.

Note that a USB CDC port delivers nothing until the host raises DTR, so a
reader that does not assert it sees a silent board rather than this output.

## Acceptance filter profile

```bash
make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=filter flash
```

Normal mode plus one **hardware** acceptance filter, chosen by
`MCP2515_TEST_FILTER`:

| Value | Accepts | Of the three frames a peer sends |
|-------|---------|----------------------------------|
| `none` (default) | every valid frame | all three |
| `id_123` | standard id `0x123` only | the standard data frame |
| `ext_only` | extended frames only | the extended data frame |

Two of the three frames must *disappear* — that is the whole point, since a
receive buffer left unfiltered accepts the entire bus and looks identical to
one that is working. This profile is also the only check on the RXF0..RXF5 /
RXM0..RXM1 bank split and on the layout a mask is written in: the controller
has two receive buffers with unequal filter banks and no way to disable
either, so a plan that left one bank open, or that positioned a mask's bits
in the wrong register, would still deliver frames it was told to reject. It
found a real bug doing exactly that; see
[`mcp2515`'s README](../../../components/mcp2515/README.md).

## Silent monitor profile

```bash
make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=monitor
```

`MCP2515_MODE_LISTEN_ONLY`: the controller neither transmits nor
acknowledges anything on the bus.

## Expected result

- Received frames print as `RX std` or `RX ext` with their identifier and
  data.
- Sent frames print once when queued. In loopback mode the very next line is
  normally the same frame received back.
- `rx_overflow` and `controller_errors` remain zero on a healthy bus;
  `last_eflg` shows the raw error-flag register when they are not. A single
  `rx_overflow` recorded as the controller joins a busy bus, which then never
  recurs, is the two hardware receive buffers filling once before the polling
  loop got to them — not a fault.

## Validated result

Run on 2026-09-04 on a Waveshare RP2350-CAN: `loopback` standalone, then
`default` and `filter` against a Waveshare RP2040-Zero running
`tests/can_test` on PIO0, at 500 kbit/s and again at 1 Mbit/s. See
[`mcp2515`'s README](../../../components/mcp2515/README.md) for what each run
established, including the bug the filter run found.
