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
firmware sends, in rotation, a standard data frame, an extended data frame, and
an RTR frame. It prints all received frames and statistics every five seconds.

The second node must use the same 500 kbit/s bitrate. It may transmit any valid
frames; no acceptance filters are enabled in this test.

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
