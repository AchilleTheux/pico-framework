# servo_bus

Dynamixel Protocol 1.0: packet encoding, and synchronous transactions over a
half-duplex UART.

## Why this exists

Dynamixel AX-12 servos and Feetech STS/SMS/SCS servos speak the *same wire
protocol* — same header, same length rule, same checksum. Only their control
tables differ, and for one Feetech family, the byte order.

The firmware this was ported from had two copies of the packet builder, one
under `AX/` and one under `Feetech/`, identical line for line. That is exactly
the duplication the framework exists to remove: a checksum bug would have
needed fixing twice. So the protocol lives here, and `ax12` and `feetech` are
register maps on top of it.

```text
      ax12            feetech
        \               /
         \             /
          servo_bus  (packets, retries, statistics)
              |
       half_duplex_uart
              |
           Pico SDK
```

## Two layers

| File | Depends on | Role |
|------|------------|------|
| `servo_protocol.c` | nothing | build and parse packets, checksums, byte order |
| `servo_bus.c` | half_duplex_uart | send, wait, validate, retry, count |

`servo_protocol.c` calls no SDK function, so the whole packet format is
unit-tested on the host — including against the worked examples in the AX-12
datasheet, which checks the implementation against the specification rather
than against itself.

## Packet format

```text
instruction:  FF FF  ID  LENGTH  INSTRUCTION  PARAM...  CHECKSUM
status:       FF FF  ID  LENGTH  ERROR        PARAM...  CHECKSUM
```

`LENGTH` counts what follows it, so a packet is `LENGTH + 4` bytes. `CHECKSUM`
is the one's complement of the sum from `ID` to the last parameter.

## Contracts

| | |
|---|---|
| **Timeouts** | Every transaction is bounded. The wait is the reply's transmission time at the *current* baud rate plus `response_timeout_us`, so changing bus speed cannot silently tighten the margin. |
| **Errors** | By return value. A servo that answers but reports a fault of its own is a **successful** transaction: the fault comes back through `error_out`, because a voltage warning is not a reason to discard a valid position reading. |
| **Buffers** | Caller-owned. Packets are built on the stack; nothing is retained. |
| **Peripheral** | Borrows a `half_duplex_uart_t` the caller owns and initialised. It does not configure or release it — with one exception, `servo_bus_set_baudrate()`, which exists because changing a servo's baud rate is only half of the operation: the host has to follow it to the new rate or the servo is gone. |
| **Retries** | On timeout, checksum failure, malformed reply or a reply from the wrong ID. The receive FIFO is flushed first, so a retry does not read the tail of whatever confused the last attempt. |

## Bus statistics

```c
const servo_bus_stats_t *stats = servo_bus_get_stats(&bus);
```

Counts transactions, retries, timeouts, checksum errors, malformed replies and
wrong-ID replies. A servo link usually degrades before it fails, and a retry
count that climbs under load points at power or termination rather than at the
code. The `servo_test` application's `soak` command exists to drive these.

## Factory reset

`servo_bus_factory_reset()` sends RESET, instruction `0x06` — a parameterless
packet, `FF FF <id> 02 06 <checksum>`, checked byte for byte by a host test
because it is the one instruction with no undo.

Protocol 1.0 has no parameter to hold anything back, so the servo's whole
EEPROM goes, ID and baud rate included. This function only sends the
instruction; the acknowledgement arrives before the servo reboots and says
nothing about the outcome. Use `ax12_factory_reset()` or
`feetech_factory_reset()`, which wait for the reboot, follow the servo to its
factory rate and confirm it came back.

## Bus speed

`servo_bus_set_baudrate()` re-clocks the host end and leaves the servos alone.
Two things need that: following a servo whose own rate has just changed, and
sweeping the rates to find a servo whose setting is unknown. An unreachable
rate is refused with the bus left running where it was, so a sweep over a list
of rates does not have to undo anything after a refusal.

`servo_bus_get_baudrate()` reports the current rate — worth reading back rather
than assuming, since the reply timeout is derived from it.

## Byte order

`SERVO_ENDIAN_LITTLE` for AX-12 and Feetech STS/SMS; `SERVO_ENDIAN_BIG` for
Feetech SCS. This is not a detail: decoding an SCS position with the STS order
turns 2048 into 8 — a number in range that looks like a servo sitting near one
end, not like a fault. Use `ax12_bus_init()` or `feetech_bus_init()` and the
choice is made for you.

## Usage

Normally reached through `ax12.h` or `feetech.h`. Directly:

```c
servo_bus_t bus;
const servo_bus_config_t config = {
    .uart = &uart,                    /* already initialised */
    .endianness = SERVO_ENDIAN_LITTLE,
    .max_retries = 2,
};
servo_bus_init(&bus, &config);

uint32_t position;
uint8_t servo_error;
if (servo_bus_read_value(&bus, 1, 0x24, 2, &position, &servo_error) == SERVO_BUS_OK) {
    /* position is valid; servo_error may still carry warning bits */
}
```

## Moving several servos together

```c
const servo_sync_target_t targets[] = {
    { .id = 1, .value = 512 },
    { .id = 2, .value = 300 },
    { .id = 3, .value = 700 },
};
ax12_sync_set_goal_positions(&bus, targets, count_of(targets));
```

One broadcast packet rather than a transaction each. The difference is timing:
separate writes mean the first servo has begun moving before the last has been
told anything — several milliseconds of skew across ten servos on a 1 Mbaud
bus, which reads as a limb that does not move as one.

What it costs is acknowledgement. A sync-write goes to the broadcast id, so
nothing replies, there is no retry, and a packet corrupted in transit is simply
not acted on. The way to find out is to read the positions back. Because of
that, the range checks happen before anything is sent: a servo told to go
somewhere impossible would otherwise fail silently.

About 42 servos fit in one packet at two bytes each;
`servo_protocol_sync_write_params()` says whether a batch will fit before you
try to build it.

## Caveats

* **Untested on hardware.** The packet format is checked against the datasheet
  by the host tests, but no servo has answered this code yet.
* **Not implemented:** `REG_WRITE`/`ACTION` and `RESET`. The instruction codes
  are defined; the operations are not. `REG_WRITE`/`ACTION` is the other way to
  make servos act together — it queues a write on each and then triggers them
  all — and is worth adding if per-servo acknowledgement of the queued write
  turns out to matter more than sync-write's single packet.
* **Protocol 2.0** (XL-320, X series) is a different format entirely and is not
  supported.

## Testing

`make test` covers the packet format, the checksum, byte order, truncation
handling and the error decoder.
