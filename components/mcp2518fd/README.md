# mcp2518fd

MCP2518FD SPI CAN controller support for RP2040 and RP2350. Link
`pico_framework::mcp2518fd` and include `mcp2518fd.h`. Supports classic CAN
2.0B and ISO CAN FD, standard/extended identifiers, classic remote requests,
FD payloads through 64 bytes, and optional bit-rate switching. Independent of
can2040 and the MCP2515 register protocol.

## Usage

The application initialises SPI and selects its pins. The driver owns only CS
and the optional active-low INT input. This example assumes an **external
MCP2518FD module with a 40 MHz crystal**, a suitable transceiver, and SPI1 on
GPIO10/11/12. The onboard XL2515 on `rp2350_can` is a different controller.

```c
#include "pico/stdlib.h"
#include "mcp2518fd.h"

static mcp2518fd_bus_t bus;

static mcp2518fd_result_t start_can(void)
{
    spi_init(spi1, 1000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(10, GPIO_FUNC_SPI); // SCK
    gpio_set_function(11, GPIO_FUNC_SPI); // MOSI -> SDI
    gpio_set_function(12, GPIO_FUNC_SPI); // MISO <- SDO
    const mcp2518fd_bus_config_t config = {
        .spi = spi1,
        .cs_pin = 9,
        .int_pin = MCP2518FD_NO_INT_PIN,
        .oscillator_hz = 40000000,
        .bitrate = 500000,
        .fd_enabled = true,
        .data_bitrate = 2000000,
        .mode = MCP2518FD_MODE_NORMAL,
    };
    return mcp2518fd_bus_init(&bus, &config);
}
```

After successful init:

```c
can_fd_message_t frame = {
    .id = 0x123, .fd = true, .bitrate_switch = true,
    .length = 12, .data = {1, 2, 3},
};
mcp2518fd_result_t result = mcp2518fd_bus_send(&bus, &frame);
// OK means queued. Handle TX_FULL by retrying later, without blocking RX.
```

Drain `mcp2518fd_bus_receive()` from the main loop until `ERR_RX_EMPTY`,
handling other errors separately. `mcp2518fd_bus_interrupt_pending()` can
skip idle polls when INT is connected. Call `mcp2518fd_bus_get_stats()`
periodically to service error/overflow flags even when not receiving.

Set `fd_enabled = false` and `data_bitrate = 0` for classic-only normal mode.
FD transmits are then rejected. FD mode permits both classic and FD traffic.
Use a message's `fd` flag to choose its format, or use
`can_fd_message_from_classic()` / `can_fd_message_to_classic()` to exchange
messages with existing `can_message_t` APIs. Conversion to classic rejects all
FD frames, even short ones. Existing classic types and queue sizes are unchanged.

## Timing and hardware

- Specify the physical oscillator: 4, 20, or 40 MHz. A 4 MHz crystal uses the
  10x PLL automatically. Other clock sources/divisors are unsupported.
- Nominal timing supports exact rates up to 1 Mbit/s. Data timing supports
  exact rates up to 8 Mbit/s, at least as fast as nominal. Zero selects
  500 kbit/s nominal and, when FD is enabled, 2 Mbit/s data.
- The solver targets an 80% sample point, then the smallest prescaler.
  Unrepresentable rates fail init; 8 Mbit/s requires 40 MHz. The 500k/2M
  calculation matches reference manual table 3-5. Automatic transmitter delay
  compensation is enabled above 1 Mbit/s. Check the resulting timing against
  bus length, transceiver delay, oscillator tolerance, and peer sample points.
- SPI uses eight-bit MSB-first mode 0 or 3. Init temporarily slows SPI, then
  applies the requested rate (or restores the caller's rate when
  `spi_baudrate_hz` is zero). Maximum: 0.425 times SYSCLK, i.e. 8.5 MHz at
  20 MHz and 17 MHz at 40 MHz. Preserve this limit when sharing SPI.
- Use a transceiver rated for the data rate, compatible 3.3 V MCU I/O, common
  ground, and termination at both bus ends. The application handles any
  transceiver standby pins. A CS pull-up keeps the controller deselected while
  the MCU is reset or its pin is released.

## Resources, filters and errors

FIFO1 holds four TX objects; FIFO2 holds sixteen RX objects. Both use 64-byte
payload slots, including in classic mode. No software queue, heap allocation,
IRQ, PIO or DMA is used. Serialise access to both this instance and the shared
SPI peripheral; the API is not reentrant or interrupt-safe. SPI is synchronous.
Full TX/empty RX return immediately; clock and mode polling is bounded to 100
attempts with 100 microseconds between attempts.

Up to 32 hardware filters have independent masks and route into FIFO2. No
filters accepts every frame. Each explicit mask must include
`CAN_FLAG_EXTENDED` and exclude `CAN_FLAG_RTR`; standard filters can mask only
the 11 identifier bits. Filter values are copied during init, so the array
need not remain alive afterwards. Example:

```c
const can_filter_t filters[] = {
    {.id = 0x123, .mask = CAN_FLAG_EXTENDED | CAN_STANDARD_ID_MAX},
    {.id = CAN_FLAG_EXTENDED | 0x1abcde0,
     .mask = CAN_FLAG_EXTENDED | CAN_EXTENDED_ID_MAX},
};
```

FD byte lengths are exactly 0–8, 12, 16, 20, 24, 32, 48, 64. No implicit
padding or identifier truncation. FD remote requests and classic BRS are
rejected. ESI is received metadata; the controller supplies its own TX state.

All SPI reads use READ_CRC and retry a mismatch at most twice. A failed read
never advances RX. Status is checked before reading RX RAM. The driver uses
status flags and user addresses, not the RX FIFO index affected by erratum 6.
RAM is fully initialised with ECC enabled; single and double ECC errors latch
`ERR_RAM`, because single-error correction is not always reliable. ECCSTAT is
preserved for diagnostics. Stop using the controller and deinit/init after
that result. Invalid FIFO addresses also latch `ERR_RAM` before RAM access.

`queued` counts accepted TX requests, **not confirmed bus transmissions**.
TEF is disabled; TX retries are unlimited. Without a peer's ACK, the four TX
slots can fill. `trec` exposes error counters and warning/passive/bus-off flags.
`rx_overflow` and `controller_errors` count observed latched events, not exact
lost-frame or bus-error counts.

Mode changes pass through configuration mode and discard queued TX/RX frames.
A failed mode change releases the pins and invalidates the instance. Deinit
makes a bounded attempt to enter configuration mode, releases CS/INT, and
leaves shared SPI running. Deinit before initialising a live instance again.
Sleep/wake, controller GPIO, timestamps, transmit completion events, and
runtime timing/filter changes are not exposed.

## Validation and references

Host tests cover wire vectors, all DLC values, IDs, filters, classic conversion,
CRC and bit timing. A simulated SPI device checks init, FIFO wraparound,
full/empty handling, CRC retries, missing devices, timeouts, ECC faults,
pointer bounds and diagnostic flags. These checks do not establish physical
CAN correctness. The [bench app](../../apps/tests/mcp2518fd_test/README.md)
provides classic, FD, loopback, filter and listen-only profiles. Builds are
checked on RP2040 and RP2350 with warnings as errors. **No MCP2518FD hardware
validation has been performed yet.**

Register protocol and workarounds follow Microchip's
[datasheet DS20006027B](https://ww1.microchip.com/downloads/en/DeviceDoc/External-CAN-FD-Controller-with-SPI-Interface-DS20006027B.pdf),
[reference manual DS20005678E](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/ReferenceManuals/MCP25XXFD-CAN-FD-Controller-Module-Family-Reference-Manual-DS20005678E.pdf),
and [errata DS80000789F](https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/Errata/MCP2518FD-Silicon-Errata-and-Data-Sheet-Clarification-DS80000789.pdf).
