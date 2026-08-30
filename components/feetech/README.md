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

## Differences from the AX-12

| | AX-12 | Feetech STS |
|---|---|---|
| Goal position register | `0x1E` | `0x2A` |
| Present position register | `0x24` | `0x38` |
| Torque enable register | `0x18` | `0x28` |
| Position range | 0..1023 over 300° | 0..4095 over 360° |
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
| Baud rate is an index, not a rate | register `0x06` takes 0..7; see `feetech_baud_index_to_rate()` |

## Caveats

* **Untested on hardware.** No Feetech servo has answered this code yet.
* **SCS is untested even in principle** — the register map here follows the STS
  series. An SCS control table differs beyond byte order, so treat
  `FEETECH_MODEL_SCS` as the byte-order switch it is, not as full SCS support.
* Register addresses follow STS3215 / STS3032 / STS2032 documentation.

## Testing

* Host: `make test` covers the control table, the byte-order difference and the
  unit conversions.
* Hardware: `make APP=tests/servo_test PROFILE=feetech_sts flash`.
