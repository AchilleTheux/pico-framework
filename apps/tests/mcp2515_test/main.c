/*
 * mcp2515_test - manual SPI CAN controller test.
 *
 * In normal mode, sends a standard frame, an extended frame, and an RTR
 * frame in rotation, prints every received frame, and periodically reports
 * diagnostic counters — the SPI-bus equivalent of can_test. In loopback
 * mode, the controller ACKs and receives its own transmissions internally,
 * so it validates the SPI wiring and this driver without a second CAN node
 * or even a transceiver.
 */

#include <stdio.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "mcp2515.h"

#ifndef MCP2515_TEST_SPI_INSTANCE
#define MCP2515_TEST_SPI_INSTANCE 1
#endif

#ifndef MCP2515_TEST_SCK_PIN
#define MCP2515_TEST_SCK_PIN 10
#endif

#ifndef MCP2515_TEST_MOSI_PIN
#define MCP2515_TEST_MOSI_PIN 11
#endif

#ifndef MCP2515_TEST_MISO_PIN
#define MCP2515_TEST_MISO_PIN 12
#endif

#ifndef MCP2515_TEST_CS_PIN
#define MCP2515_TEST_CS_PIN 9
#endif

#ifndef MCP2515_TEST_INT_PIN
#define MCP2515_TEST_INT_PIN 8
#endif

#ifndef MCP2515_TEST_SPI_BAUDRATE_HZ
#define MCP2515_TEST_SPI_BAUDRATE_HZ 1000000u
#endif

#ifndef MCP2515_TEST_OSCILLATOR_HZ
#define MCP2515_TEST_OSCILLATOR_HZ 16000000u
#endif

#ifndef MCP2515_TEST_BITRATE
#define MCP2515_TEST_BITRATE MCP2515_DEFAULT_BITRATE
#endif

#ifndef MCP2515_TEST_MODE
#define MCP2515_TEST_MODE MCP2515_MODE_NORMAL
#endif

/*
 * Hardware acceptance filter to install; see MCP2515_TEST_FILTER in
 * CMakeLists.txt. Same two settings as can_test's software equivalent, and
 * the same reason for having them: an unfiltered receive buffer and a
 * correctly filtered one are indistinguishable until traffic that should be
 * rejected arrives. Here that also proves the RXF0..RXF5 / RXM0..RXM1 bank
 * split, since either receive buffer left open would deliver the rest of
 * the bus.
 */
#define MCP2515_TEST_FILTER_NONE 0
#define MCP2515_TEST_FILTER_ID_123 1
#define MCP2515_TEST_FILTER_EXT_ONLY 2

#ifndef MCP2515_TEST_FILTER
#define MCP2515_TEST_FILTER MCP2515_TEST_FILTER_NONE
#endif

#define SEND_INTERVAL_MS 1000u
#define STATS_INTERVAL_MS 5000u

static mcp2515_bus_t bus;
static can_filter_t filters[1];

/* Fills `filters`, names the choice, and returns how many were installed. */
static size_t build_filters(const char **description)
{
#if MCP2515_TEST_FILTER == MCP2515_TEST_FILTER_ID_123
    filters[0].id = can_id_pack(0x123, false, false);
    filters[0].mask = CAN_STANDARD_ID_MAX | CAN_FLAG_EXTENDED;
    *description = "standard id 0x123 only";
    return 1;
#elif MCP2515_TEST_FILTER == MCP2515_TEST_FILTER_EXT_ONLY
    filters[0].id = CAN_FLAG_EXTENDED;
    filters[0].mask = CAN_FLAG_EXTENDED;
    *description = "extended frames only";
    return 1;
#else
    (void)filters;
    *description = "none: every valid frame accepted";
    return 0;
#endif
}

static spi_inst_t *selected_spi(void)
{
#if MCP2515_TEST_SPI_INSTANCE == 0
    return spi0;
#elif MCP2515_TEST_SPI_INSTANCE == 1
    return spi1;
#else
#error "MCP2515_TEST_SPI_INSTANCE must be 0 or 1"
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
    mcp2515_bus_stats_t stats;
    if (mcp2515_bus_get_stats(&bus, &stats) != MCP2515_OK) {
        return;
    }
    printf("stats rx=%lu tx=%lu rx_overflow=%lu controller_errors=%lu "
           "last_eflg=0x%02x waiting=%u\n",
           (unsigned long)stats.received,
           (unsigned long)stats.transmitted,
           (unsigned long)stats.rx_overflow,
           (unsigned long)stats.controller_errors,
           stats.last_eflg,
           (unsigned)mcp2515_bus_available(&bus));
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    spi_inst_t *spi = selected_spi();
    spi_init(spi, MCP2515_TEST_SPI_BAUDRATE_HZ);
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MCP2515_TEST_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MCP2515_TEST_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MCP2515_TEST_MISO_PIN, GPIO_FUNC_SPI);

    const char *filter_description = NULL;
    const size_t filter_count = build_filters(&filter_description);

    const mcp2515_bus_config_t config = {
        .spi = spi,
        .cs_pin = MCP2515_TEST_CS_PIN,
        .int_pin = MCP2515_TEST_INT_PIN,
        .oscillator_hz = MCP2515_TEST_OSCILLATOR_HZ,
        .bitrate = MCP2515_TEST_BITRATE,
        .mode = MCP2515_TEST_MODE,
        .filters = filter_count != 0 ? filters : NULL,
        .filter_count = filter_count,
    };

    const mcp2515_result_t init = mcp2515_bus_init(&bus, &config);
    if (init != MCP2515_OK) {
        while (true) {
            printf("mcp2515_bus_init failed: %s\n", mcp2515_result_name(init));
            sleep_ms(1000);
        }
    }

    printf("\nmcp2515_test: board=%s spi=%d cs=%d int=%d oscillator=%luHz bitrate=%lu mode=%d\n",
           PICO_BOARD, MCP2515_TEST_SPI_INSTANCE, MCP2515_TEST_CS_PIN, MCP2515_TEST_INT_PIN,
           (unsigned long)MCP2515_TEST_OSCILLATOR_HZ, (unsigned long)MCP2515_TEST_BITRATE,
           (int)MCP2515_TEST_MODE);
    printf("filter: %s\n", filter_description);
    if (MCP2515_TEST_MODE == MCP2515_MODE_LOOPBACK) {
        puts("loopback self-test: transmissions loop back internally, nothing goes on the wire");
    } else if (MCP2515_TEST_MODE == MCP2515_MODE_LISTEN_ONLY) {
        puts("silent monitor mode: no frames or acknowledgements will be transmitted");
    } else {
        puts("sending one frame per second; another active node must acknowledge it");
    }

    uint32_t sequence = 0;
    absolute_time_t next_send = make_timeout_time_ms(SEND_INTERVAL_MS);
    absolute_time_t next_stats = make_timeout_time_ms(STATS_INTERVAL_MS);

    while (true) {
        if (mcp2515_bus_interrupt_pending(&bus)) {
            can_message_t received;
            while (mcp2515_bus_receive(&bus, &received) == MCP2515_OK) {
                print_message("RX", &received);
            }
        }

        if (MCP2515_TEST_MODE != MCP2515_MODE_LISTEN_ONLY && time_reached(next_send)) {
            const can_message_t outgoing = make_test_message(sequence++);
            const mcp2515_result_t result = mcp2515_bus_send(&bus, &outgoing);
            if (result == MCP2515_OK) {
                print_message("TX", &outgoing);
            } else {
                printf("TX refused: %s\n", mcp2515_result_name(result));
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
