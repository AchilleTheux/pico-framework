# mcp2515

CAN 2.0B over an MCP2515 SPI controller — or a pin/register-compatible clone,
such as the XL2515 on Waveshare's [`rp2350_can`](../../boards/rp2350_can.h)
board. Applications link `pico_framework::mcp2515` and include `mcp2515.h`.

Unlike [`can`](../can/) (can2040 on a dedicated PIO block, fed by an
interrupt), this is a device on a shared SPI bus, in the same spirit as
[`i2c_device`](../i2c_device/): the application owns and configures the SPI
peripheral and its SCK/MOSI/MISO pins, and this component only ever drives
chip select and the register protocol over it. Frame, filter, and queue types
(`can_message_t`, `can_filter_t`) come from [`can_frame`](../can_frame/), so
code that already speaks to `can` reads and writes the same shapes here.

The MCP2515 needs no transceiver-adjacent PIO or DMA resources of its own —
the controller itself already speaks the CAN protocol and hands off to a
3.3 V/5 V-compatible CAN transceiver on its TXCAN/RXCAN pins, same as
`can`'s pins connect to one.

## No interrupt-context receive path

`can` gets frames from can2040's own PIO interrupt and hands them to the
application through a queue. This component has no equivalent: an MCP2515 is
a synchronously-polled SPI peripheral, and running SPI transactions from
inside a GPIO interrupt handler is exactly the kind of latency/priority
inversion risk this framework's PIO-based CAN component goes to some length
to avoid. Instead:

* `mcp2515_bus_receive()` polls the controller's status register over SPI
  and pops a message if one is waiting. Call it often enough that the
  controller's **two-message hardware RX depth** does not overflow —
  `mcp2515_bus_get_stats()` reports `rx_overflow` when it does.
* `int_pin`, if wired, lets the main loop skip that poll cheaply while the
  bus is idle: `mcp2515_bus_interrupt_pending()` reads the pin directly, no
  SPI transaction involved. It is optional — `MCP2515_NO_INT_PIN` makes it
  always report true, so the caller just polls unconditionally.

## Basic use

```c
#include "hardware/spi.h"
#include "mcp2515.h"

static mcp2515_bus_t bus;

void start_can(void)
{
    /* The application owns spi1 and its pins; several devices may share it. */
    spi_init(spi1, 1000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(10, GPIO_FUNC_SPI); /* SCK  */
    gpio_set_function(11, GPIO_FUNC_SPI); /* MOSI */
    gpio_set_function(12, GPIO_FUNC_SPI); /* MISO */

    const mcp2515_bus_config_t config = {
        .spi = spi1,
        .cs_pin = 9,
        .int_pin = 8,           /* MCP2515_NO_INT_PIN to always poll */
        .oscillator_hz = 8000000,
        .bitrate = 500000,
        .mode = MCP2515_MODE_NORMAL,
    };
    mcp2515_bus_init(&bus, &config);
}

void poll_can(void)
{
    if (!mcp2515_bus_interrupt_pending(&bus)) {
        return; /* nothing waiting; skip the SPI transaction entirely */
    }
    can_message_t received;
    while (mcp2515_bus_receive(&bus, &received) == MCP2515_OK) {
        /* handle received */
    }
}
```

`mcp2515_bus_send()` picks a free one of the controller's three TX buffers
and requests transmission; it returns `MCP2515_ERR_TX_FULL` rather than
blocking when all three are busy.

## The oscillator is a fixed physical fact, not a guess

`oscillator_hz` has no default — it is whatever crystal or resonator is
soldered to the controller, and getting it wrong silently mistimes every bit
on the bus rather than failing to build. Confirm it against the board's
schematic or crystal silkscreen. It normally belongs in the board header next
to the pin assignments (see `rp2350_can.h`'s `RP2350_CAN_XL2515_OSCILLATOR_HZ`,
confirmed at 16 MHz against that board's schematic).

`mcp2515_bus_init()` fails with `MCP2515_ERR_INVALID_ARG` if
`mcp2515_compute_bit_timing()` cannot reach the requested bit rate *exactly*
from that oscillator — see [`mcp2515_timing.h`](include/mcp2515_timing.h) for
why an inexact answer is refused rather than approximated. Not every
oscillator/bit-rate pair is achievable; 8 MHz cannot reach 1 Mbit/s, for
example. Standard combinations most modules ship with — a small number of
common crystal frequencies (8, 16, 20 MHz) against the standard CAN bit rates
— all resolve.

## Hardware filters are real hardware, not a software list

`can`'s software filters accept an arbitrary-length list, each entry
independent. The MCP2515 has **two independent three-filter banks**: filters
`[0..2]` share one mask register (RXM0), filters `[3..5]` share a second
(RXM1). Every filter within a bank must use the identical `.mask` —
`mcp2515_bus_init()` validates this and returns `MCP2515_ERR_INVALID_ARG`
otherwise. An empty filter set accepts everything, the same convention as
`can`.

Two further hardware limits, also validated at init:

* every filter's mask must include `CAN_FLAG_EXTENDED` — the controller
  always compares a filter's standard/extended flag exactly, it cannot mask
  that bit off the way `can`'s software filters can;
* no filter's mask may include `CAN_FLAG_RTR` — the controller has no
  hardware remote-request filter at all.

RXB0 has rollover enabled unconditionally (`BUKT`): when it fills, the next
message rolls into RXB1 instead of being dropped, buying a little more
headroom against a slow polling loop before `rx_overflow` starts counting.

## Modes

`MCP2515_MODE_NORMAL` is the ordinary case. `MCP2515_MODE_LISTEN_ONLY` is the
SPI-bus equivalent of `can`'s `CAN_NO_TX_PIN`: receive-only, no transmission
or acknowledgement, `mcp2515_bus_send()` returns `MCP2515_ERR_MONITOR_MODE`.
`MCP2515_MODE_LOOPBACK` is unique to this controller: it ACKs and receives
its own transmissions internally, so it validates the SPI wiring, bit
timing, and this driver's register handling with **no second CAN node, and
no transceiver** — see `apps/tests/mcp2515_test`'s `loopback` profile.
`mcp2515_bus_set_mode()` switches at runtime; both it and `mcp2515_bus_init()`
block briefly (bounded, `MCP2515_ERR_MODE_TIMEOUT` on failure) for the
controller to confirm the switch.

## Diagnostics

`mcp2515_bus_get_stats()` reports frames received, frames the controller
confirmed sent (drained from its `TXnIF` flags), RX overflow events, and a
generic controller-error counter with the raw `EFLG` register snapshot at
the last one, for a caller that wants more detail than this driver decodes
itself.

## Concurrency

Not reentrant. `mcp2515_bus_init()`, `mcp2515_bus_deinit()`, and every other
call on one `mcp2515_bus_t` must come from the same context — there is no
interrupt-safe path here the way `can_bus_send()` inherits from can2040.

## Testing

`tests/components/mcp2515_timing_test.c` verifies bit-timing register
computation is exact (not approximated) and stays within the controller's
valid field ranges, across a spread of oscillator/bit-rate pairs.
`tests/components/mcp2515_frame_test.c` covers SIDH/SIDL/EID8/EID0/DLC
packing round-trips for standard, extended, data, and remote frames, and
pins the exact bit position of the extended and remote-request flags.
`apps/tests/mcp2515_test` is the manual hardware test, with `default`
(normal, needs a second node and transceivers), `loopback` (self-test, needs
neither), and `monitor` (silent, receive-only) profiles.
