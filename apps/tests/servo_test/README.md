# servo_test

Interactive bench for the `ax12` and `feetech` components: a serial command
line onto a servo bus.

Unlike the other hardware tests, this is not a pass/fail self-check. A servo bus
needs a servo, and once one is attached what you want is to poke at it — scan
the bus, read registers by name, move something, and watch whether the link
holds up.

It is also the framework's composition claim in one file: `cli`,
`half_duplex_uart`, `ax12`/`feetech` and optionally `ws2812`, none of which
knows about any of the others.

## Required hardware

* Any RP2040 or RP2350 board.
* At least one AX-12 or Feetech servo.
* **A servo power supply.** These servos want 7–12 V at several amps under
  load; they cannot run from the board's 3V3 or VBUS.
* Usually a transceiver or level shifter — see below.

## Wiring

```text
   board GPIO 21  ────────────┬──────────  servo DATA
                              │
                          pull-up to 3V3
                          (often already on the servo board)

   board GND      ───────────────────────  servo GND ── supply GND
   supply +V      ───────────────────────  servo V+
```

Grounds must be common between board, servos and supply.

Boards with a transceiver steer it from a direction pin; set
`SERVO_TEST_DIRECTION_PIN` in a profile. With the pin left at `-1` the GPIO
drives the bus directly, which works for a short lead and one or two servos.

## Profiles

| Profile | Bus |
|---------|-----|
| `ax12` | Dynamixel AX-12 / AX-18, 1 Mbaud |
| `feetech_sts` | Feetech STS / SMS |
| `feetech_scs` | Feetech SCS — big-endian registers |
| `ax12_leds` | AX-12 plus a WS2812 strip showing the last transaction's outcome |

Which family is on the bus is a property of the bench, not of the code, so it
is a profile rather than a second near-identical application. The linker drops
whichever servo component the profile did not select: the `ax12` build contains
16 `ax12_` symbols and no `feetech_` ones, and vice versa.

## Running

```bash
make BOARD=pico2 APP=tests/servo_test PROFILE=ax12
make BOARD=pico2 APP=tests/servo_test PROFILE=ax12 flash
picocom -b 115200 /dev/ttyACM0
```

## Commands

```text
servo> help
servo> scan                    find every servo on the bus
servo> ping 1
servo> regs                    list the control table with widths
servo> read 1 present_position registers by name, or by address
servo> read 1 0x24
servo> write 1 led 1
servo> torque 1 1              nothing moves until this is on
servo> pos 1                   where is it
servo> pos 1 512               go there
servo> status 1                position, speed, load, temperature, voltage
servo> baud 500000             change bus speed on the fly
servo> soak 1 1000             hammer the link and count failures
servo> stats                   retries, timeouts, checksum errors
servo> clear
```

## Suggested bring-up order

1. `scan` — with one servo attached, exactly one ID should answer. If none
   does, the problem is wiring or baud rate, not the servo.
2. `read 1 model_number` — proves a real reply, checksum and all, not just a
   line that idles at the right level.
3. `torque 1 1`, then `pos 1 512` — proves the write path.
4. `status 1` — proves multi-byte reads decode correctly. A position that
   changes when you move the horn by hand is the strongest signal that byte
   order and register widths are right.
5. `soak 1 1000`, then `stats` — proves the link is *reliable*, not merely
   working. Zero retries over a thousand reads is a healthy bus.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `scan` finds nothing | baud rate mismatch (a factory servo is 1 Mbaud), wiring, or no servo power |
| `scan` finds every ID | the bus is echoing rather than answering — the `echo` setting does not match the wiring |
| ping works, `read` times out | the servo's status return level is set to answer pings only |
| positions look plausible but wrong | byte order: an SCS servo on the STS profile turns 2048 into 8 |
| position reads halve or double | register width — check `regs` against the servo's datasheet |
| servo answers but will not move | torque is off, or the goal is outside the configured angle limits |
| `stats` shows climbing retries | power sag, a missing pull-up, or too long a lead — the link is marginal rather than broken |
| `error: reply from another id` | two servos share an ID |

## What this cannot tell you

That a servo is mechanically healthy. A servo with a stripped gear reports
positions perfectly and does not move.
