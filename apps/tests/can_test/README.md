# can_test

Manual hardware test for the PIO/can2040 component on RP2040 and RP2350.

## Required hardware

- An RP2040 or RP2350 board.
- A 3.3 V CAN transceiver for each test node, for example SN65HVD230.
- A second active CAN node. A transmitter cannot complete successfully unless
  another node acknowledges it.
- 120 ohm termination at each end of the bus (60 ohms measured between CANH
  and CANL with power off when both terminators are fitted).

Do not connect CANH/CANL directly to GPIO pins. Classical CAN requires a
transceiver.

## Default wiring

| Transceiver | Board |
|-------------|-------|
| VCC | 3V3 |
| GND | GND, common with every bus node |
| RXD | GPIO 4 |
| TXD | GPIO 5 |
| CANH | CANH bus |
| CANL | CANL bus |

The exact transceiver pin names vary. Check its datasheet, especially standby
or slope-control pins that may need biasing.

## Build and run

```bash
make BOARD=pico APP=tests/can_test PROFILE=default
make BOARD=pico APP=tests/can_test PROFILE=default flash
```

Use `BOARD=pico2` for RP2350. Open USB CDC or the default UART at 115200. The
firmware sends, in rotation, a standard data frame with id `0x123`, an
extended data frame with id `0x01ABCDE0`, and an RTR frame with id `0x321`. It
prints all received frames and statistics every five seconds.

The second node must use the same 500 kbit/s bitrate. It may transmit any valid
frames.

Note that a USB CDC port delivers nothing until the host raises DTR, so a
reader that does not assert it sees a silent board rather than this output.

## RP2040-Zero profile

```bash
make BOARD=rp2040_zero APP=tests/can_test PROFILE=rp2040_zero flash
```

For a transceiver on the board's bottom-edge pads: RXD on GPIO 29, TXD on
GPIO 28. Neither pin has another function there. This is the profile the
component was validated with, paired with
`BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=default` on the same bus, so
the PIO backend and an MCP2515 acknowledge each other.

## Pico 2 W profiles (RP2350)

```bash
make BOARD=pico2_w APP=tests/can_test PROFILE=pico2_w flash
make BOARD=pico2_w APP=tests/can_test PROFILE=pio2   flash
```

Transceiver RXD on GPIO 26, TXD on GPIO 27 — clear of the four pins the CYW43
radio takes (23, 24, 25, 29). `pico2_w` uses PIO0; `pio2` is the same wiring
on PIO block **2**, which exists only on RP2350 and which no other profile
reaches.

Running the RP2350 as well as the RP2040 is not redundancy. can2040 selects a
different bit stuffer and unstuffer per chip, writes an rp2350-only PIO
register, and gains a third PIO block there — so an RP2040 result does not
carry over. See [`can`'s README](../../../components/can/README.md).

## Acceptance filter profile

```bash
make BOARD=rp2040_zero APP=tests/can_test PROFILE=filter flash
```

The same wiring plus one software filter. `CAN_TEST_FILTER` selects it:

| Value | Accepts | Of the three frames a peer sends |
|-------|---------|----------------------------------|
| `none` (default) | every valid frame | all three |
| `id_123` | standard id `0x123` only | the standard data frame |
| `ext_only` | extended frames only | the extended data frame |

A filter that is quietly accepting everything looks exactly like one that
works, so the point of the rotation is that two of the three frames must
*disappear*: one `RX` line per three the peer sends, with `filtered` rising by
two for each one accepted. The node keeps transmitting all three regardless —
a receive filter is not a transmit filter.

## Silent monitor profile

```bash
make BOARD=pico2 APP=tests/can_test PROFILE=monitor
```

This passes no TX pin to can2040, so the firmware neither transmits nor
acknowledges. Ensure the transceiver TXD input is held high/recessive externally.
The default wiring's GPIO 5 is not configured in this profile.

## Expected result

- Received frames print as `RX std` or `RX ext` with their identifier and data.
- Sent frames print once when queued. The `tx` statistic increases only after
  another node acknowledges them.
- `parse`, `queue_drop`, and `controller_overflow` remain zero on a healthy bus.
- `attempts` may exceed `tx` briefly during arbitration, but a rapidly growing
  difference usually means missing acknowledgements, wrong bitrate, wiring, or
  termination.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `tx=0`, attempts rises | no second node acknowledges, wrong bitrate, or broken bus wiring |
| parse errors rise | noise, wrong bitrate, poor grounding, or termination |
| controller overflows rise | CAN IRQ latency is too high |
| queue drops rise | main loop is not draining received frames fast enough |
| no received traffic in monitor mode | TXD not held recessive, wrong RX pin, or no correctly acknowledged traffic between other nodes |
| init reports PIO/IRQ in use | another component already owns the selected PIO block |
| `parse` and `attempts` climbing together in the tens of thousands | nothing is acknowledging: no second node, wrong bitrate, or a wiring/termination fault. Expected, and harmless, while a node waits alone for its peer to boot — the counters freeze the moment one appears |
| no output at all on USB CDC | the reader is not asserting DTR |
| a burst of identical frames, with `queue_drop` raised but no longer rising | this application prints every frame it receives, so an unread USB console eventually blocks its main loop: the queue overflows, the peer stops being acknowledged and retries one frame, and the copies that were received print all at once when the console drains. A test-harness artefact, not a bus fault — in a continuously read session the stream is one frame per type per second with no repeats |
| every frame filtered out, or the wrong one getting through | check `CAN_TEST_FILTER` against the table above; the printed `filter:` line at startup says which is compiled in |

## Validated result

Run on 2026-09-04 against a Waveshare RP2350-CAN running `tests/mcp2515_test`
as the peer: `PROFILE=rp2040_zero` on an RP2040-Zero and `PROFILE=pico2_w` on
a Pico 2 W, each at 500 kbit/s and 1 Mbit/s, plus `filter` on the RP2040 and
`pio2` on the RP2350. See
[`can`'s README](../../../components/can/README.md) for what each run
established, and for the two RP2350B-only paths still uncovered.
