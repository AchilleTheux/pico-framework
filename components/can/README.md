# CAN

Classic CAN 2.0B for RP2040 and RP2350, implemented with
[can2040](../../lib/can2040/README.md) on one PIO block. Applications link
`pico_framework::can` and include `can.h`.

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
