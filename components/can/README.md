# CAN

Classic CAN 2.0B for RP2040 and RP2350, implemented with
[can2040](../../lib/can2040/README.md) on one PIO block. Applications link
`pico_framework::can` and include `can.h`.

The frame, filter, and queue types (`can_message_t`, `can_filter_t`,
`can_queue_t`) live in [`can_frame`](../can_frame/), shared with the SPI-based
[`mcp2515`](../mcp2515/) controller driver. `can.h` pulls it in transitively;
applications only ever link `pico_framework::can` or `pico_framework::mcp2515`
directly.

This is a controller driver, not a transceiver. The MCU pins must connect to a
3.3 V-compatible CAN transceiver such as an SN65HVD230. The transceiver connects
to CANH/CANL and the physical bus still needs its normal termination.

## Resource and timing contract

can2040 uses **all four state machines, all 32 instruction slots, and IRQ 0** of
the selected PIO block. `can_bus_init()` claims all four state machines and
refuses to start if either the PIO or IRQ is already in use. RP2040 can therefore
run at most two independent CAN controllers; RP2350 can run at most three.

The interrupt handler is latency-sensitive. At 1 Mbit/s, roughly seven
microseconds of latency can already delay acknowledgements. Keep CAN at high IRQ
priority and avoid long interrupt-disabled sections on its core. Production
applications should run can2040 from RAM; the hardware test does this with
`pico_set_binary_type(... copy_to_ram)`.

The component claims SDK resources but can2040 programs the PIO registers and
instruction memory directly. Do not access or reconfigure the selected PIO
block while the bus is active.

## Basic use

```c
#include "can.h"

static can_bus_t can;
static uint8_t rx_storage[CAN_QUEUE_STORAGE_SIZE(32)];

static const can_filter_t filters[] = {
    {
        .id = 0x120,
        .mask = 0x7F8 | CAN_FLAG_EXTENDED,
    },
};

void start_can(void)
{
    const can_bus_config_t config = {
        .pio = pio0,
        .rx_pin = 4,
        .tx_pin = 5,
        .bitrate = 500000,
        .filters = filters,
        .filter_count = 1,
        .rx_storage = rx_storage,
        .rx_storage_size = sizeof(rx_storage),
        .irq_priority = 0,
    };
    can_bus_init(&can, &config);
}
```

The can2040 callback runs in IRQ context. It applies the filters and copies each
accepted frame into `rx_storage`; application code later calls
`can_bus_receive()`. An empty filter set accepts everything. Filters include the
packed identifier flags, so standard/extended and data/remote frames can be
selected independently.

`can_bus_send()` validates identifier width and payload length before handing a
frame to can2040. Its four-entry transmit FIFO returns `CAN_ERR_TX_FULL` instead
of blocking.

## Silent monitoring

Set `tx_pin = CAN_NO_TX_PIN` to receive without transmitting or acknowledging.
The physical transceiver's TXD input must still be held recessive (normally
high); can2040 cannot configure a pin it has not been given. `can_bus_send()`
returns `CAN_ERR_MONITOR_MODE` in this configuration.

## Queues and concurrency

Receive storage is caller-owned and holds whole 16-byte records. Allocate it
with `CAN_QUEUE_STORAGE_SIZE(frame_count)`. When it fills, a complete frame is
dropped and `queue_dropped` increases—partial frames are never published.

The receive queue is single-producer/single-consumer and same-core: initialise
and consume it on the core where the PIO IRQ is enabled. `can_bus_send()` and
`can_bus_get_stats()` inherit can2040's cross-core-safe operations, but lifecycle
operations are not reentrant. Do not call `can_bus_init()` or `can_bus_deinit()`
concurrently with any other operation.

## Diagnostics

`can_bus_get_stats()` reports wire traffic, transmit attempts, parse errors,
software-filter rejects, application queue drops, and can2040 FIFO overflows.
The distinctions matter: a filter reject is intentional; a full application
queue means the main loop is too slow; a controller overflow means IRQ latency
was already too high.

## License of the dependency

can2040 is licensed under GNU GPLv3; see
[its COPYING file](../../lib/can2040/COPYING). Firmware linking this component
must comply with that license.

## Testing

`tests/components/can_frame_test.c` covers frame validation, flag packing, DLC
handling, filters, FIFO order, wraparound, full-queue drops, and invalid input on
the host. `apps/tests/can_test` is the manual transceiver/bus test for both MCU
families.

## Hardware validation

Validated on 2026-09-04 on **both** MCU families, each time against a
Waveshare RP2350-CAN running [`mcp2515`](../mcp2515/) on its onboard XL2515 as
the second node. Every result is therefore also a cross-controller
interoperability result: this PIO implementation and a hardware CAN controller
acknowledged each other's frames on one bus.

Both chips were run because **can2040 is not the same code on the two of
them**, and an RP2040 pass says nothing about RP2350. Its bit stuffer and bit
unstuffer each dispatch to a separate rp2040 or rp2350 implementation, chosen
by the `PICO_RP2350` the SDK defines for us; it writes the rp2350-only PIO
`gpiobase` register; and RP2350 has a third PIO block, whose reset bit,
`pio2_hw` selection, and `case 2` IRQ handler here are code an RP2040 build
does not even compile.

### RP2040 — Waveshare RP2040-Zero, `rp2040_zero` profile

PIO0, transceiver RXD on GPIO 29 and TXD on GPIO 28.

* 500 kbit/s, both nodes active: standard, extended, and RTR frames delivered
  in both directions with identifiers, DLCs, and payloads intact — 66 frames
  each way over 60 s, `received` and `transmitted` advancing 5 per 5 s with no
  drift, and `queue_drop` and `controller_overflow` flat at zero.
* The same at **1 Mbit/s**, `CAN_MAX_BITRATE`, where the IRQ-latency margin
  the contract above describes is at its tightest: still no controller
  overflows and no parse errors, from a `copy_to_ram` build.
* Started against a live peer, a healthy bus reads exactly as this
  component's diagnostics promise: `attempts == tx` and `parse == 0`.
* `filter` profile, one software filter for standard id `0x123`: only those
  frames reached the application, `filtered` accounting for the other two
  thirds of the peer's traffic, and the node's own transmissions unaffected.

### RP2350 — Pico 2 W, `pico2_w` and `pio2` profiles

Transceiver RXD on GPIO 26 and TXD on GPIO 27, at a 150 MHz `clk_sys` rather
than the RP2040's 125 MHz.

* 500 kbit/s on PIO0: 65 frames each way over 60 s, thirteen consecutive
  counter samples every one of which advanced by exactly 5, `attempts == tx`
  throughout, and `parse`, `queue_drop`, and `controller_overflow` all zero for
  the whole run — no drift, no refusals, no repeated payloads.
* **1 Mbit/s** on PIO0: all three frame types both directions, still no
  overflows and no drops.
* 500 kbit/s on **PIO block 2**, the block that exists only on this chip and
  that no other profile reaches: `can_bus_init()` claimed it without error and
  the run was as clean as PIO0's.

Two RP2350-specific paths remain uncovered, and both are unreachable on an
RP2350**A**: a non-zero PIO `gpiobase` and this component's
`#if NUM_BANK0_GPIOS > 32` pin-window check both need a pin above 31, so they
want an RP2350**B** board with 48 GPIOs. The `gpiobase` register write itself
does execute here, with the value 0.

Two behaviours worth knowing before reading a log, both observed during these
runs and both reported correctly by these diagnostics.

A node left **alone** on the bus does not merely fail to transmit: can2040
retries continuously, and each unacknowledged attempt is followed by an error
frame it counts as a parse error, so `attempts` and `parse` climb together at
tens of thousands per second. Once the peer came up, both counters froze at
their startup totals and never moved again. A large but *static* `parse` count
is a node that spent time alone; a growing one is a bus fault.

And the receive queue really is only as fast as the loop draining it. The test
application prints every frame it receives, so leaving its USB console unread
eventually blocks that loop: `queue_dropped` rose to 106 and then froze, and
the peer — unacknowledged meanwhile — retried a single frame whose received
copies all printed at once when the console drained. Nothing was silently
lost; the counter said exactly how much was, which is what it is for.
