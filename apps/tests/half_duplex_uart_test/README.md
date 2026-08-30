# half_duplex_uart_test

Hardware test for the `half_duplex_uart` component.

## Required hardware

**Nothing.** Any RP2040 or RP2350 board, with no wiring at all.

That is the point: on a shared-wire bus the receiver hears the transmitter, so
running under `HALF_DUPLEX_UART_ECHO_KEEP` turns the bus into a loopback. The
PIO programs, the clock divider and the line handover are all exercised without
a servo present.

Leave GPIO 21 (or whatever `HDX_TEST_PIN` is set to) unconnected.

## Running

```bash
make BOARD=pico2 APP=tests/half_duplex_uart_test PROFILE=default
make BOARD=pico2 APP=tests/half_duplex_uart_test PROFILE=default flash
picocom -b 115200 /dev/ttyACM0
```

| Profile | Setup |
|---------|-------|
| `default` | bare pin 21, no direction pin, 1 Mbaud |
| `transceiver` | pin 21 with a direction signal on GPIO 27, matching the Carte_actionneurs wiring |

## Expected result

Each pass prints the timing, then one line per check:

```text
half_duplex_uart_test  board=pico2 pin=21 dir=-1 baud=1000000

--- pass 0 ---
  timing: divider 18+192/256, actual 1000000 baud, error 0/1000
  [pass] single byte                         1 bytes intact
  [pass] all zero bits                       8 bytes intact
  [pass] all one bits                        8 bytes intact
  [pass] alternating bits                    8 bytes intact
  [pass] all 256 byte values                 0x00 to 0xFF intact
  [pass] loopback at 57600 baud              actual 57600 baud, error 0/1000
  ...
  [pass] echo suppression                    write ok, 0 stray bytes after
  9 checks, 0 failed
```

All checks should pass. The counts are printed so a failure is obvious without
reading every line.

## What each check is for

| Check | Catches |
|-------|---------|
| single byte | the basic start/data/stop framing |
| all zero bits, all one bits | bit timing errors — neither pattern has a transition to resynchronise on, so they fail first when the divider is wrong |
| alternating bits | bit ordering, and sampling on the wrong edge |
| all 256 byte values | a single bit position that is wrong only for some values |
| loopback at each baud | that `set_baudrate()` retimes both state machines together, and that the reported error matches reality |
| echo suppression | that `write()` consumes exactly its own echo — off by one and every servo reply would be shifted by a byte |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| everything times out, 0 bytes received | the pin is being held low externally, or `HDX_TEST_PIN` is wired to something |
| single byte passes, all-zeroes fails | bit timing: check the reported divider error |
| some byte values corrupted, others fine | shift direction or bit count in the PIO program |
| low rates pass, 1 Mbaud fails | system clock is not what the divider assumed, or the pad's slew rate is limiting |
| echo suppression reports stray bytes | `write()` is discarding the wrong count, or something else is driving the bus |
| `init failed: baudrate unreachable` | the rate needs a divider outside 1..65536, or is more than 2% off |

## What this test does not cover

A real bus: multiple devices, an external pull-up, a transceiver, and a device
that answers. That comes with the `ax12` and `feetech` hardware tests, which
build on this component.
