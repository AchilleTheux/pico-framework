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
| Both angle limits 0 is continuous rotation | goal position is then ignored and `ax12_set_moving_speed()` steers |
| `ax12_scan()` takes about a second | it pings all 254 IDs, most of which time out. A bring-up tool, not a control-loop call |

## Caveats

**Untested on hardware.** The packets match the datasheet's worked examples and
the register table is host-tested, but no AX-12 has answered this code yet.

## Testing

* Host: `make test` covers the control table, the EEPROM boundary and the unit
  conversions.
* Hardware: `make APP=tests/servo_test PROFILE=ax12 flash` — an interactive
  bench. See that application's README.
