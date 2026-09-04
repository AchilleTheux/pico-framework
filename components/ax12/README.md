# ax12

Dynamixel AX-12 and AX-18 servos.

A control table and named operations over [`servo_bus`](../servo_bus/), which
supplies the packet format, retries and transport.

## No handle type

There is no `ax12_t`. A servo has no state on this side of the wire, so every
call takes the bus and an ID:

```c
half_duplex_uart_t uart;
servo_bus_t bus;

half_duplex_uart_init(&uart, &uart_config);
ax12_bus_init(&bus, &uart);          /* picks the right byte order */

ax12_set_torque_enable(&bus, 1, true);
ax12_set_moving_speed(&bus, 1, 200);
ax12_set_goal_position(&bus, 1, 512);

uint16_t position;
ax12_get_present_position(&bus, 1, &position);
```

Link it:

```cmake
target_link_libraries(app_my_firmware PRIVATE pico_framework::ax12)
```

`servo_bus` and `half_duplex_uart` come along transitively — an application
never names the transport (DESIGN_DOC.md section 7).

## The control table

`ax12_register_width()` is the reason to prefer `ax12_read_register()` over a
raw `servo_bus_read()`. Reading a two-byte register as one byte returns its low
half: a number in range that looks like a plausible position. A wrong width is
a **silent wrong answer**, not a failure, so the width comes from a table that
the host tests check register by register against the datasheet.

Addressing the high half of a word register — `0x1F` instead of `0x1E` — reports
width 0 rather than 1, so that typo is caught rather than half-executed.

`ax12_register_is_eeprom()` marks the registers that wear out on write and
survive power-off.

## Baud rate

`AX12_REG_BAUD_RATE` holds a divisor, not a rate: the bus runs at
`2000000 / (value + 1)`. The datasheet's names for the low rates are rounded,
and the rounding matters — a servo set to "115200" actually runs at **117647
baud**, 2% away, which is inside a UART's tolerance on its own but not once the
host's own clock quantisation is added on top.

`ax12_baud_value_to_rate()` and `ax12_rate_to_baud_value()` therefore deal in
exact rates, and accept either spelling: 115200 and 117647 select the same
divisor.

Changing a servo's rate is only half the operation — the host has to follow, or
the servo is simply gone. `ax12_set_baudrate()` does both ends and pings to
confirm:

```c
ax12_set_baudrate(&bus, 1, 115200);   /* servo and bus both end at 117647 */
```

It writes nothing when the rate is unreachable at either end, and puts the bus
back where it was if the servo does not answer at the new rate — so a failure
costs neither an EEPROM write nor a lost servo. Other servos on the bus are
left behind at the old rate; pass `SERVO_PROTOCOL_BROADCAST_ID` to move them
all at once, and check with `ax12_scan()` afterwards since nothing
acknowledges a broadcast.

## Finding a servo whose settings are unknown

A servo that has been used before answers nothing until the host happens to be
clocked at the rate in its EEPROM, and that is indistinguishable from a wiring
fault. `ax12_baud_rate_table()` gives the nine rates a servo can be at,
fastest first, for sweeping:

```c
size_t count;
const uint32_t *rates = ax12_baud_rate_table(&count);

for (size_t i = 0; i < count; i++) {
    if (servo_bus_set_baudrate(&bus, rates[i]) != SERVO_BUS_OK) {
        continue;               /* this host cannot be clocked there */
    }
    uint8_t ids[16];
    size_t found;
    ax12_scan(&bus, ids, count_of(ids), &found);
    /* ... */
}
```

The `discover` command in `apps/tests/servo_test` is this loop with the output
formatted; use it rather than writing it again.

## Factory reset

`ax12_factory_reset()` restores the whole EEPROM and follows the servo back:

```c
ax12_factory_reset(&bus, 7);   /* comes back as id 1 at 1 Mbaud */
```

Protocol 1.0's RESET (instruction `0x06`) carries no parameters, so there is no
"everything except the ID" form — that exists only in Protocol 2.0. Everything
goes, the ID and the baud rate included, which has three consequences worth
knowing before typing it:

* **One servo at a time.** Two servos reset on one bus both answer as id 1
  afterwards. The call refuses `SERVO_PROTOCOL_BROADCAST_ID` for that reason;
  give each servo its ID back with `ax12_set_id()` before resetting the next.
* **It is not the way out of an unknown baud rate.** The servo has to hear the
  instruction, so `discover` comes first. Reset is for putting a servo you *can*
  reach into a known state.
* **The acknowledgement means nothing.** It is sent before the servo reboots.
  The function follows the bus to 1 Mbaud and polls id 1 until it answers or
  `AX12_RESET_TIMEOUT_MS` runs out, so `SERVO_BUS_OK` means the servo is
  actually back — not that a packet went out.

Nothing is sent when the host cannot be clocked at the factory rate, and if the
servo does not come back the bus is returned to the rate it was running.

## Units

Angles are integer millidegrees, not floats. The servo's own step is about 293
millidegrees, so nothing is lost, and integer arithmetic keeps the conversions
exactly testable — the tests check that every one of the 1024 positions
survives a round trip and that the conversion lands on the *nearest* angle.

```c
ax12_set_goal_millidegrees(&bus, 1, 150000);   /* 150.000 degrees */
```

Present speed and load use a sign-magnitude encoding with the direction in bit
10; `ax12_decode_signed_magnitude()` turns that into a signed value, negative
for clockwise. Feetech uses bit 15 instead, which is why the two decoders are
separate functions rather than one shared one.

## Things that will catch you out

| | |
|---|---|
| Torque is **off** at power-up | nothing moves until `ax12_set_torque_enable()`, however healthy the pings look |
| Speed 0 means "no limit" | not "stop". Setting speed 0 makes the next move as fast as the servo goes |
| An ID change takes effect immediately | the servo stops answering the old ID the moment the write lands |
| So does a baud-rate change | and the acknowledgement may be sent at either rate, so a lost reply to that write means nothing. `ax12_set_baudrate()` pings to find out instead |
| "115200" is 117647 baud | the datasheet's names for the low rates are rounded; clock the host from `ax12_baud_value_to_rate()`, not from the name |
| Both angle limits 0 is continuous rotation | goal position is then ignored and `ax12_set_moving_speed()` steers |
| A factory reset resets the ID too | Protocol 1.0 has no "all except the ID" form, so reset one servo at a time or they all come back as id 1 |
| `ax12_scan()` takes about a second | it pings all 254 IDs, most of which time out. A bring-up tool, not a control-loop call |

## Caveats

**Untested on hardware.** The packets match the datasheet's worked examples and
the register table is host-tested, but no AX-12 has answered this code yet.

## Testing

* Host: `make test` covers the control table, the EEPROM boundary, the unit
  conversions and the baud-rate divisors.
* Hardware: `make APP=tests/servo_test PROFILE=ax12 flash` — an interactive
  bench. See that application's README.
