/*
 * can_test - manual CAN transceiver and bus test.
 *
 * Sends a standard frame, an extended frame, and an RTR frame in rotation,
 * prints every received frame, and periodically reports diagnostic counters.
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "can.h"

#ifndef CAN_TEST_PIO
#define CAN_TEST_PIO 0
#endif

#ifndef CAN_TEST_RX_PIN
#define CAN_TEST_RX_PIN 4
#endif

#ifndef CAN_TEST_TX_PIN
#define CAN_TEST_TX_PIN 5
#endif

#ifndef CAN_TEST_BITRATE
#define CAN_TEST_BITRATE CAN_DEFAULT_BITRATE
#endif

#define RX_QUEUE_FRAMES 64u
#define SEND_INTERVAL_MS 1000u
#define STATS_INTERVAL_MS 5000u

static can_bus_t bus;
static uint8_t rx_storage[CAN_QUEUE_STORAGE_SIZE(RX_QUEUE_FRAMES)];

static PIO selected_pio(void)
{
#if CAN_TEST_PIO == 0
    return pio0;
#elif CAN_TEST_PIO == 1
    return pio1;
#elif CAN_TEST_PIO == 2 && NUM_PIOS > 2
    return pio2;
#else
#error "CAN_TEST_PIO is not available on this chip"
#endif
}

static void print_message(const char *direction, const can_message_t *message)
{
    printf("%s %s id=%08lx %s dlc=%u data=", direction,
           message->extended ? "ext" : "std",
           (unsigned long)message->id,
           message->remote ? "RTR" : "data",
           message->length);
    if (message->remote) {
        printf("-");
    } else {
        for (uint8_t i = 0; i < message->length; i++) {
            printf("%02x", message->data[i]);
        }
    }
    printf("\n");
}

static can_message_t make_test_message(uint32_t sequence)
{
    can_message_t message = { 0 };
    switch (sequence % 3u) {
        case 0:
            message.id = 0x123;
            message.length = 8;
            for (uint8_t i = 0; i < message.length; i++) {
                message.data[i] = (uint8_t)(sequence + i);
            }
            break;
        case 1:
            message.id = 0x01ABCDE0u;
            message.extended = true;
            message.length = 4;
            message.data[0] = (uint8_t)sequence;
            message.data[1] = (uint8_t)(sequence >> 8);
            message.data[2] = 0x23;
            message.data[3] = 0x50;
            break;
        default:
            message.id = 0x321;
            message.remote = true;
            message.length = 2;
            break;
    }
    return message;
}

static void print_stats(void)
{
    can_bus_stats_t stats;
    if (can_bus_get_stats(&bus, &stats) != CAN_OK) {
        return;
    }
    printf("stats rx=%lu tx=%lu attempts=%lu parse=%lu filtered=%lu "
           "queue_drop=%lu controller_overflow=%lu waiting=%u\n",
           (unsigned long)stats.received,
           (unsigned long)stats.transmitted,
           (unsigned long)stats.transmit_attempts,
           (unsigned long)stats.parse_errors,
           (unsigned long)stats.filtered,
           (unsigned long)stats.queue_dropped,
           (unsigned long)stats.controller_errors,
           (unsigned)can_bus_available(&bus));
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    const can_bus_config_t config = {
        .pio = selected_pio(),
        .rx_pin = CAN_TEST_RX_PIN,
        .tx_pin = CAN_TEST_TX_PIN,
        .bitrate = CAN_TEST_BITRATE,
        .rx_storage = rx_storage,
        .rx_storage_size = sizeof(rx_storage),
        .irq_priority = 0,
    };

    const can_result_t init = can_bus_init(&bus, &config);
    if (init != CAN_OK) {
        while (true) {
            printf("can_bus_init failed: %s\n", can_result_name(init));
            sleep_ms(1000);
        }
    }

    printf("\ncan_test: board=%s pio=%d rx=%d tx=%d bitrate=%d\n",
           PICO_BOARD, CAN_TEST_PIO, CAN_TEST_RX_PIN, CAN_TEST_TX_PIN,
           CAN_TEST_BITRATE);
    if (CAN_TEST_TX_PIN == CAN_NO_TX_PIN) {
        puts("silent monitor mode: no frames or acknowledgements will be transmitted");
    } else {
        puts("sending one frame per second; another active node must acknowledge it");
    }

    uint32_t sequence = 0;
    absolute_time_t next_send = make_timeout_time_ms(SEND_INTERVAL_MS);
    absolute_time_t next_stats = make_timeout_time_ms(STATS_INTERVAL_MS);

    while (true) {
        can_message_t received;
        while (can_bus_receive(&bus, &received) == CAN_OK) {
            print_message("RX", &received);
        }

        if (CAN_TEST_TX_PIN != CAN_NO_TX_PIN && time_reached(next_send)) {
            const can_message_t outgoing = make_test_message(sequence++);
            const can_result_t result = can_bus_send(&bus, &outgoing);
            if (result == CAN_OK) {
                print_message("TX", &outgoing);
            } else {
                printf("TX refused: %s\n", can_result_name(result));
            }
            next_send = make_timeout_time_ms(SEND_INTERVAL_MS);
        }

        if (time_reached(next_stats)) {
            print_stats();
            next_stats = make_timeout_time_ms(STATS_INTERVAL_MS);
        }
        tight_loop_contents();
    }
}
