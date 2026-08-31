# half_duplex_uart

8N1 UART over a single shared wire, driven by two PIO state machines.

Built for daisy-chained smart servos — Dynamixel AX-12, Feetech STS — where one
line carries both directions and the host always speaks first. It is the shared
transport the `ax12` and `feetech` components are meant to sit on, so neither
has to reimplement bit timing or line turnaround.

## With or without a level shifter

The direction pin is optional, and independent of which servo family is on the
bus:

| Wiring | `direction_pin` |
|--------|-----------------|
| GPIO straight to the servo data line | `HALF_DUPLEX_UART_NO_DIRECTION_PIN` |
| Through a bidirectional level shifter or transceiver | the GPIO steering it — driven high while transmitting, low while receiving |

Both work with `ax12` and with `feetech`. The firmware this was ported from
happened to use a shifter for its AX-12 bus and none for its Feetech bus, and
had a *separate PIO program* for each. Here it is one program and one config
field, so the choice follows the board rather than the servo brand.

Side-set carries the direction signal. With no direction pin it maps to the
data pin instead, where the levels it drives are the ones the line should be at
anyway.

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

## The echo, and why the default is what it is

Both state machines sit on the same pad, so the receiver hears every byte the
transmitter sends. That is true **with or without a level shifter**: a
bidirectional shifter passes our own drive straight back to the pad.

| `echo` | For |
|---|---|
| `HALF_DUPLEX_UART_ECHO_DISCARD` (default, 0) | any wiring where transmit and receive share a pad — direct, or through a level shifter. `write()` waits for exactly as many bytes as it sent and consumes them, so a following `read()` sees only the reply. |
| `HALF_DUPLEX_UART_ECHO_KEEP` | a transceiver that mutes its receiver while driving, or a caller that wants to read back what it sent — which is what makes the loopback hardware test need no wiring. |

The zero value is `DISCARD` deliberately, because the two mistakes are not
equally bad:

* Discarding an echo that never arrives ends in a clean
  `HALF_DUPLEX_UART_ERR_TIMEOUT` from `write()`.
* **Failing to discard one that does arrive is far worse.** The echoed request
  is itself a well-formed packet with a valid checksum, so it parses as a
  plausible status reply and the caller gets confident nonsense rather than an
  error. A host test in `servo_protocol_test.c` demonstrates this.

So a zero-initialised config gets the behaviour that fails loudly.

The echo is *waited for*, not cleared blindly: the last byte may still be in
flight when the state machine goes idle, and clearing the FIFO then would drop
the first byte of the reply instead.

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
    .echo = HALF_DUPLEX_UART_ECHO_DISCARD,   /* the default */
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

* **Bare-pin loopback is hardware-validated.** On an RP2040-Zero the shared
  GPIO path passed every byte value, echo suppression, and 57,600 through
  1,000,000 baud (`11 checks, 0 failed`). A direction-controlled transceiver
  and a real servo bus have not been tested.
* **Pull-up.** The driver enables the pad's internal pull-up so a released line
  idles high. A real bus usually has a stronger external pull-up as well.
* **GPIO base on RP2350B.** For pins above GPIO 31, set the PIO block's GPIO
  base before `init()`.
* **No direction pin?** Side-set still has to map somewhere, so it maps to the
  data pin, where the values it drives (idle high before the start bit) are
  what the line should be doing anyway. This is the wiring with no level
  shifter at all, and it is fully supported — pass
  `HALF_DUPLEX_UART_NO_DIRECTION_PIN`.

## Testing

* Host: `make test` covers the divider arithmetic and frame timing.
* Hardware: `make APP=tests/half_duplex_uart_test flash` — a loopback self-test
  that needs **nothing connected**, because the receiver hears the transmitter.
  See that test's README.
