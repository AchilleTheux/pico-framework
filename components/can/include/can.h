/*
 * can - CAN 2.0B on RP2040 and RP2350 using can2040 and one PIO block.
 *
 * can2040 owns all four state machines, all 32 instructions, and IRQ 0 of the
 * selected PIO block. The component claims those resources in the Pico SDK so
 * another driver cannot unknowingly reuse them.
 *
 * Receive callbacks run in interrupt context. They do only bounded filtering
 * and copy accepted frames into caller-owned storage; applications consume the
 * queue from their main loop. No memory is allocated dynamically.
 */

#ifndef PICO_FRAMEWORK_CAN_H
#define PICO_FRAMEWORK_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/pio.h"

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "can2040.h"

#define CAN_DEFAULT_BITRATE 500000u
#define CAN_MAX_BITRATE 1000000u

/* Select receive-only silent monitoring. See can_bus_config_t::tx_pin. */
#define CAN_NO_TX_PIN (-1)

typedef enum {
    CAN_OK = 0,
    CAN_ERR_INVALID_ARG,
    CAN_ERR_PIO_IN_USE,
    CAN_ERR_IRQ_IN_USE,
    CAN_ERR_NOT_INITIALISED,
    CAN_ERR_MONITOR_MODE,
    CAN_ERR_TX_FULL,
    CAN_ERR_RX_EMPTY,
} can_result_t;

typedef struct {
    /* The component takes exclusive ownership of this entire PIO block. */
    PIO pio;

    /* MCU pins connected to RXD and TXD on an external CAN transceiver. */
    int rx_pin;
    int tx_pin; /* CAN_NO_TX_PIN for silent, receive-only monitoring. */

    /* 0 selects CAN_DEFAULT_BITRATE. can2040 supports up to 1 Mbit/s. */
    uint32_t bitrate;

    /*
     * Optional software acceptance filters. The array must remain valid until
     * can_bus_deinit(). No filters means accept every valid frame.
     */
    const can_filter_t *filters;
    size_t filter_count;

    /* Caller-owned receive queue storage. Use CAN_QUEUE_STORAGE_SIZE(n). */
    uint8_t *rx_storage;
    size_t rx_storage_size;

    /*
     * CPU interrupt priority passed to irq_set_priority(). Zero is the
     * highest priority and is the recommended/default value for can2040.
     */
    uint8_t irq_priority;
} can_bus_config_t;

typedef struct {
    uint32_t received;          /* all valid frames observed by can2040 */
    uint32_t transmitted;       /* frames successfully put on the bus */
    uint32_t transmit_attempts; /* includes arbitration and retry attempts */
    uint32_t parse_errors;      /* malformed/noisy traffic seen on the wire */
    uint32_t filtered;          /* valid frames rejected by software filters */
    uint32_t queue_dropped;     /* accepted frames lost to a full app queue */
    uint32_t controller_errors; /* can2040 internal receive FIFO overflows */
} can_bus_stats_t;

/*
 * Public so instances can live in static storage; fields are private to the
 * component. In particular, applications must not call can2040 directly on
 * `controller` or alter the receive queue indices.
 */
typedef struct {
    struct can2040 controller; /* must remain the first member; see can.c */
    can_queue_t rx_queue;
    PIO pio;
    const can_filter_t *filters;
    size_t filter_count;
    uint32_t bitrate;
    volatile uint32_t filtered;
    volatile uint32_t controller_errors;
    uint irq_num;
    int rx_pin;
    int tx_pin;
    bool initialised;
} can_bus_t;

/*
 * Start a bus and enable its PIO IRQ. The caller must call this on the core
 * that will consume the receive queue and must keep that core's interrupt
 * latency low. Returns a resource error instead of asserting if the PIO block
 * or IRQ is already owned.
 */
can_result_t can_bus_init(can_bus_t *bus, const can_bus_config_t *config);

/* Stop the controller and release the IRQ and all four PIO state machines. */
void can_bus_deinit(can_bus_t *bus);

/* Queue one frame for transmission. can2040 has four transmit slots. */
can_result_t can_bus_send(can_bus_t *bus, const can_message_t *message);

/* Whether can_bus_send() can currently accept a frame. */
bool can_bus_can_send(can_bus_t *bus);

/* Pop the oldest accepted frame copied out of interrupt context. */
can_result_t can_bus_receive(can_bus_t *bus, can_message_t *message);

size_t can_bus_available(const can_bus_t *bus);
void can_bus_clear_receive(can_bus_t *bus);

/* Snapshot counters safely while the PIO interrupt may still be running. */
can_result_t can_bus_get_stats(can_bus_t *bus, can_bus_stats_t *stats);

const char *can_result_name(can_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CAN_H */
