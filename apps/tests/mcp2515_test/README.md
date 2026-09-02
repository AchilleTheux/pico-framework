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
valid frames; no acceptance filters are enabled in this test.

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
  `last_eflg` shows the raw error-flag register when they are not.
