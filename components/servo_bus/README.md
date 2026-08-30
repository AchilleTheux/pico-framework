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
| **Peripheral** | Borrows a `half_duplex_uart_t` the caller owns and initialised. It does not configure or release it. |
| **Retries** | On timeout, checksum failure, malformed reply or a reply from the wrong ID. The receive FIFO is flushed first, so a retry does not read the tail of whatever confused the last attempt. |

## Bus statistics

```c
const servo_bus_stats_t *stats = servo_bus_get_stats(&bus);
```

Counts transactions, retries, timeouts, checksum errors, malformed replies and
wrong-ID replies. A servo link usually degrades before it fails, and a retry
count that climbs under load points at power or termination rather than at the
code. The `servo_test` application's `soak` command exists to drive these.

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

## Caveats

* **Untested on hardware.** The packet format is checked against the datasheet
  by the host tests, but no servo has answered this code yet.
* **Not implemented:** `SYNC_WRITE`, `REG_WRITE`/`ACTION`, and `RESET`. The
  instruction codes are defined; the operations are not. Sync-write is the one
  worth adding first, for moving several servos in the same instant.
* **Protocol 2.0** (XL-320, X series) is a different format entirely and is not
  supported.

## Testing

`make test` covers the packet format, the checksum, byte order, truncation
handling and the error decoder.
