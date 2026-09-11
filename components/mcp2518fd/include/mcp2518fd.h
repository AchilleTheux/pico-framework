#ifndef PICO_FRAMEWORK_MCP2518FD_H
#define PICO_FRAMEWORK_MCP2518FD_H

#include "hardware/spi.h"
#include "mcp2518fd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP2518FD_DEFAULT_BITRATE 500000u
#define MCP2518FD_DEFAULT_DATA_BITRATE 2000000u
#define MCP2518FD_NO_INT_PIN (-1)
#define MCP2518FD_TX_DEPTH 4u
#define MCP2518FD_RX_DEPTH 16u

typedef enum {
    MCP2518FD_OK = 0,
    MCP2518FD_ERR_INVALID_ARG,
    MCP2518FD_ERR_NOT_INITIALISED,
    MCP2518FD_ERR_NO_DEVICE,
    MCP2518FD_ERR_SPI,          /* three consecutive CRC read failures */
    MCP2518FD_ERR_TIMEOUT,
    MCP2518FD_ERR_MONITOR_MODE,
    MCP2518FD_ERR_TX_FULL,
    MCP2518FD_ERR_RX_EMPTY,
    MCP2518FD_ERR_INVALID_FRAME,
    MCP2518FD_ERR_RAM,          /* ECC or invalid FIFO address; reinitialise */
} mcp2518fd_result_t;

typedef enum {
    MCP2518FD_MODE_NORMAL = 0,
    MCP2518FD_MODE_LOOPBACK,    /* internal loopback, no bus traffic */
    MCP2518FD_MODE_LISTEN_ONLY,
} mcp2518fd_mode_t;

typedef struct {
    /* Caller owns SPI init, SCK/MOSI/MISO, and serialisation of the shared
       bus. Use 8-bit, MSB-first, mode 0 or 3. No IRQ, PIO or DMA is claimed. */
    spi_inst_t *spi;
    uint cs_pin;
    int int_pin;               /* active-low INT, or MCP2518FD_NO_INT_PIN */
    /* 0 keeps the caller's baud rate after init. Init temporarily slows SPI
       for oscillator startup. Maximum is 0.425 * SYSCLK, including rounding
       by the Pico SPI divider. Caller must preserve that limit afterwards. */
    uint32_t spi_baudrate_hz;
    uint32_t oscillator_hz;    /* physical crystal: 4, 20, or 40 MHz; required */
    uint32_t bitrate;          /* 0 -> 500 kbit/s */
    bool fd_enabled;           /* false -> classic CAN only */
    uint32_t data_bitrate;     /* FD: 0 -> 2 Mbit/s; classic: must be 0 */
    mcp2518fd_mode_t mode;
    /* Copied into hardware at init; no pointer retained. NULL/0 accepts all.
       Up to 32 independent filters; restrictions in mcp2518fd_frame.h. */
    const can_filter_t *filters;
    size_t filter_count;
} mcp2518fd_bus_config_t;

typedef struct {
    uint32_t received;
    uint32_t queued;           /* accepted by TX FIFO, NOT confirmed sent */
    uint32_t rx_overflow;      /* observed overflow flags, not lost-frame count */
    uint32_t invalid_frames;
    uint32_t spi_crc_errors;   /* includes recovered read attempts */
    uint32_t controller_errors;/* observed CERRIF/SERRIF events */
    uint32_t trec;             /* raw CiTREC: TEC, REC, warning/passive/bus-off */
    uint32_t eccstat;          /* latched raw ECCSTAT; any SEC/DED needs reinit */
} mcp2518fd_bus_stats_t;

/* Caller-owned storage; fields private. No heap allocation. */
typedef struct {
    spi_inst_t *spi;
    uint cs_pin;
    int int_pin;
    bool initialised;
    bool fd_enabled;
    bool ram_fault;
    mcp2518fd_mode_t mode;
    mcp2518fd_bus_stats_t stats;
} mcp2518fd_bus_t;

/* All operations are synchronous SPI polling, not reentrant/IRQ-safe.
   Serialise all users of this instance AND the shared SPI peripheral.
   Deinit before reusing a live instance. Failed init releases owned pins.
   Start with the chip powered and awake; sleep/wake is not implemented. */
mcp2518fd_result_t mcp2518fd_bus_init(mcp2518fd_bus_t *bus, const mcp2518fd_bus_config_t *config);
void mcp2518fd_bus_deinit(mcp2518fd_bus_t *bus);

/* Bounded operations: TX full / RX empty return immediately. Receiving is
   polled; drain promptly to avoid overflowing the 16-message hardware FIFO.
   Output message is unchanged on error. */
mcp2518fd_result_t mcp2518fd_bus_send(mcp2518fd_bus_t *bus, const can_fd_message_t *message);
mcp2518fd_result_t mcp2518fd_bus_receive(mcp2518fd_bus_t *bus, can_fd_message_t *message);
bool mcp2518fd_bus_can_send(mcp2518fd_bus_t *bus);
/* A hint only: false when no enabled interrupt is pending. Without an INT
   pin always returns true on an initialised bus. Does not service errors. */
bool mcp2518fd_bus_interrupt_pending(mcp2518fd_bus_t *bus);
/* Mode changes preserve bit timing, but pass through configuration mode and
   discard pending TX/RX frames. Counters remain cumulative. */
mcp2518fd_result_t mcp2518fd_bus_set_mode(mcp2518fd_bus_t *bus, mcp2518fd_mode_t mode);
/* Services overflow/error flags; call periodically even on an idle bus.
   Stats are also returned on ERR_RAM, so ECC diagnostics remain accessible. */
mcp2518fd_result_t mcp2518fd_bus_get_stats(mcp2518fd_bus_t *bus, mcp2518fd_bus_stats_t *stats);
const char *mcp2518fd_result_name(mcp2518fd_result_t result);

#ifdef __cplusplus
}
#endif
#endif
