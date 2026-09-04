# feetech

Feetech STS, SMS and SCS smart servos.

The same shape as [`ax12`](../ax12/), and for the same reason: both speak
Dynamixel Protocol 1.0, so both are a control table and named operations over
[`servo_bus`](../servo_bus/). What differs is the register map and the byte
order.

```c
half_duplex_uart_t uart;
servo_bus_t bus;

half_duplex_uart_init(&uart, &uart_config);
feetech_bus_init(&bus, &uart, FEETECH_MODEL_STS);

feetech_set_torque_enable(&bus, 1, true);
feetech_set_goal_position(&bus, 1, 2048);      /* mid-travel: 0..4095 */
```

## Model families and byte order

| Family | Registers | Position range |
|--------|-----------|----------------|
| `FEETECH_MODEL_STS` (STS, SMS) | little-endian | 0..4095 over 360 degrees |
| `FEETECH_MODEL_SCS` | **big**-endian | 0..4095 over 360 degrees |

This is the single most consequential difference, and it is not detectable at
runtime. Decoding an SCS position with the STS byte order turns 2048 into 8: a
value in range that looks like a servo resting near one end, not like a fault.
`feetech_bus_init()` takes the model so the choice is made once, and a host
test demonstrates the failure explicitly rather than describing it.

## Baud rate

`FEETECH_REG_BAUD_RATE` holds an index into a fixed table of eight rates, not a
rate — `feetech_baud_index_to_rate()` and `feetech_rate_to_baud_index()` convert
in both directions, so nothing has to remember that 4 means 115200.

Changing a servo's rate is only half the operation — the host has to follow, or
the servo is simply gone. `feetech_set_baudrate()` does both ends: it unlocks
the EEPROM, writes the index, re-clocks the bus, pings to confirm, and locks
the EEPROM again at the new rate.

```c
feetech_set_baudrate(&bus, 1, 115200);
```

It writes nothing when the rate is unreachable at either end, and if the servo
does not answer at the new rate it puts the bus back and re-locks there — so
neither a refusal nor a failure leaves an EEPROM open or a servo lost. Other
servos on the bus are left behind at the old rate; `SERVO_PROTOCOL_BROADCAST_ID`
moves them all at once, but nothing acknowledges a broadcast, so that form can
neither confirm anything nor re-lock: follow it with `feetech_scan()` and
`feetech_lock_eeprom()` per servo.

## Finding a servo whose settings are unknown

A servo that has been used before answers nothing until the host happens to be
clocked at the rate in its EEPROM, and that is indistinguishable from a wiring
fault. `feetech_baud_rate_table()` gives the eight rates, fastest first, for
sweeping — set the host to each with `servo_bus_set_baudrate()` and
`feetech_scan()` at each. The `discover` command in `apps/tests/servo_test` is
that loop with the output formatted.

## Factory reset

`feetech_factory_reset()` restores the whole EEPROM and follows the servo back
to id 1 at 1 Mbaud. It unlocks the EEPROM first — whether a Feetech servo needs
that before honouring a reset is not documented either way, and unlocking costs
one write to a RAM register.

Everything goes, the ID included, so it is a one-servo operation and
`SERVO_PROTOCOL_BROADCAST_ID` is refused: two servos reset on one bus both
answer as id 1 afterwards. It is also **not** the way out of an unknown baud
rate — the servo has to hear the instruction first, so `discover` comes before
this. See the [ax12 README](../ax12/) for the reasoning, which is the same for
both families since RESET belongs to the shared protocol.

## Differences from the AX-12

| | AX-12 | Feetech STS |
|---|---|---|
| Goal position register | `0x1E` | `0x2A` |
| Present position register | `0x24` | `0x38` |
| Torque enable register | `0x18` | `0x28` |
| Position range | 0..1023 over 300° | 0..4095 over 360° |
| Baud register | a divisor, `2000000/(n+1)` | an index into a table of 8 |
| Direction bit | bit 10 | bit 15 |
| EEPROM writes | direct | need `feetech_unlock_eeprom()` first |

A host test asserts these register addresses actually differ, so a copy-paste
between the two components cannot go unnoticed.

## Things that will catch you out

| | |
|---|---|
| Torque is **off** at power-up | the most common reason a new servo pings fine and refuses to move |
| The EEPROM is locked | an ID or baud-rate change silently does nothing until unlocked. `feetech_set_id()` unlocks, writes, and re-locks using the *new* ID, because the change is immediate |
| A new servo is ID 1 at 1 Mbaud | put two unconfigured servos on one bus and they will both answer |
| Baud rate is an index, not a rate | register `0x06` takes 0..7; see `feetech_baud_index_to_rate()`. Writing 4 asks for 115200 here and 400000 on an AX-12 |
| A baud-rate change takes effect immediately | and the acknowledgement may be sent at either rate, so a lost reply to that write means nothing. `feetech_set_baudrate()` pings to find out instead |

## Caveats

* **Untested on hardware.** No Feetech servo has answered this code yet.
* **SCS is untested even in principle** — the register map here follows the STS
  series. An SCS control table differs beyond byte order, so treat
  `FEETECH_MODEL_SCS` as the byte-order switch it is, not as full SCS support.
* Register addresses follow STS3215 / STS3032 / STS2032 documentation.

## Testing

* Host: `make test` covers the control table, the byte-order difference, the
  unit conversions and the baud-rate table.
* Hardware: `make APP=tests/servo_test PROFILE=feetech_sts flash`.
