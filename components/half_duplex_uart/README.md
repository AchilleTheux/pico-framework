# half_duplex_uart

8N1 UART over a single shared wire, driven by two PIO state machines.

Built for daisy-chained smart servos — Dynamixel AX-12, Feetech STS — where one
line carries both directions and the host always speaks first. It is the shared
transport the `ax12` and `feetech` components are meant to sit on, so neither
has to reimplement bit timing or line turnaround.

## How the wire is shared

The transmit program takes the line only while a byte is going out and releases
it afterwards:

```text
    pull            side 0   ; idle: line released, transceiver receiving
    set pindirs, 1  side 1   ; drive the line, transceiver transmitting
    ...  start bit, 8 data bits, stop bit  ...
    set pindirs, 0           ; release
```

Both state machines are mapped to the same pad, so the receiver is listening
the whole time — including while we transmit.

## The echo, and why it is a config flag

On the usual wiring, the receiver hears every byte the transmitter sends. The
driver handles it, but which behaviour is right depends on the board, so it is
explicit:

| `receives_own_transmission` | For |
|---|---|
| `true` | transmit and receive share a pad with nothing muting the echo — the common servo-bus wiring. `write()` waits for exactly as many bytes as it sent and discards them, so a following `read()` sees only the reply. |
| `false` | a transceiver that disables its receiver while driving. Leaving this `true` there would eat the first bytes of every reply. |

The echo is *waited for*, not cleared blindly: the last byte may still be in
flight when the state machine goes idle, and clearing the FIFO then would drop
the first byte of the reply instead. If the echo never arrives, `write()`
returns `HALF_DUPLEX_UART_ERR_TIMEOUT` rather than handing back a shifted reply.

## Contracts

Section 8 of the design document asks a transport to state these up front.

| | |
|---|---|
| **Timeouts** | Every read takes an explicit timeout. Nothing blocks forever. `read()` also takes an inter-byte timeout: once a reply starts, a quiet line means it ended. |
| **Errors** | By return value. A framing error drops the byte inside the PIO program, so it surfaces as a short reply rather than as garbage. |
| **Buffers** | Caller-owned, always. The component allocates nothing and retains no pointer past the call. |
| **Peripheral** | Claims two state machines and both programs on the PIO block it is given, and owns them until `deinit()`. The data and direction pins belong to it while initialised. |
| **Concurrency** | Not interrupt-driven, not thread-safe. One bus, one calling context. |

## Baud rate accuracy

The PIO clock divider is 8.8 fixed point, so most rates cannot be hit exactly.
`init()` and `set_baudrate()` **fail** rather than accept a rate more than
`HALF_DUPLEX_UART_MAX_ERROR_PERMILLE` (2%) off, because a bus clocked slightly
wrong works until it doesn't — the failure is intermittent and expensive to
chase.

1 Mbaud, the rate AX-12 and Feetech buses run at, is exact on both platforms:

| System clock | Divider | Actual |
|---|---|---|
| 125 MHz (RP2040) | 15 + 160/256 | 1000000 baud, 0 error |
| 150 MHz (RP2350) | 18 + 192/256 | 1000000 baud, 0 error |

`half_duplex_uart_get_timing()` reports what the bus is really clocked at. The
arithmetic is in `half_duplex_uart_timing.c`, which has no SDK dependency and
is unit-tested on the host.

## Usage

```c
#include "half_duplex_uart.h"

static half_duplex_uart_t bus;

const half_duplex_uart_config_t config = {
    .pio = pio0,
    .pin = 21,                       /* from the board header */
    .direction_pin = 27,             /* or HALF_DUPLEX_UART_NO_DIRECTION_PIN */
    .baudrate = 1000000,
    .receives_own_transmission = true,
};

if (half_duplex_uart_init(&bus, &config) != HALF_DUPLEX_UART_OK) {
    /* unreachable baud rate, or no free state machine */
}

/* One servo transaction: send a packet, read a reply of known length. */
uint8_t request[8] = { /* ... */ };
uint8_t reply[8];

if (half_duplex_uart_transfer(&bus, request, sizeof(request),
                              reply, sizeof(reply),
                              /* timeout */ 3000) == HALF_DUPLEX_UART_OK) {
    /* reply is complete */
}
```

Link it from the application, or from a component that builds on it:

```cmake
target_link_libraries(pico_framework_ax12 PUBLIC pico_framework::half_duplex_uart)
```

## Resource use

Two state machines and 17 instructions on one PIO block. A PIO block holds 32
instructions and 4 state machines, so one bus plus a `ws2812` strip fit
together with room left over.

## Caveats

* **Untested on hardware.** The port is complete and builds clean for both
  architectures, but no servo bus has been on the end of it yet. The loopback
  test application is the way to check it; see below.
* **Pull-up.** The driver enables the pad's internal pull-up so a released line
  idles high. A real bus usually has a stronger external pull-up as well.
* **GPIO base on RP2350B.** For pins above GPIO 31, set the PIO block's GPIO
  base before `init()`.
* **No direction pin?** Side-set still has to map somewhere, so it maps to the
  data pin, where the values it drives match what the line does anyway.

## Testing

* Host: `make test` covers the divider arithmetic and frame timing.
* Hardware: `make APP=tests/half_duplex_uart_test flash` — a loopback self-test
  that needs **nothing connected**, because the receiver hears the transmitter.
  See that test's README.
