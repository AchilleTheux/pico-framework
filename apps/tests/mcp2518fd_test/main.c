#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "mcp2518fd.h"

static mcp2518fd_bus_t bus;

static can_fd_message_t test_message(uint32_t sequence)
{
    can_fd_message_t message = {.id = 0x123, .length = 8};
    switch (sequence % (MCP2518FD_TEST_FD ? 5u : 3u)) {
        case 1:
            message.id = 0x1abcde0;
            message.extended = true;
            break;
        case 2:
            message.id = 0x321;
            message.remote = true;
            break;
        case 3:
            message.fd = true;
            message.length = 12;
            break;
        case 4:
            message.id = 0x1abcde0;
            message.extended = true;
            message.fd = true;
            message.bitrate_switch = true;
            message.length = 64;
            break;
        default: break;
    }
    if (!message.remote) {
        for (unsigned i = 0; i < message.length; i++) message.data[i] = (uint8_t)(sequence + i);
    }
    return message;
}

static void print_message(const char *direction, const can_fd_message_t *message)
{
    printf("%s %s %s id=%08lx len=%u RTR=%u BRS=%u ESI=%u data=", direction,
           message->fd ? "FD" : "classic", message->extended ? "ext" : "std",
           (unsigned long)message->id, message->length, message->remote,
           message->bitrate_switch, message->error_state_indicator);
    if (!message->remote) {
        for (unsigned i = 0; i < message->length; i++) printf("%02x", message->data[i]);
    }
    printf("\n");
}

static bool equal_message(const can_fd_message_t *a, const can_fd_message_t *b)
{
    return a->id == b->id && a->length == b->length && a->extended == b->extended &&
           a->remote == b->remote && a->fd == b->fd && a->bitrate_switch == b->bitrate_switch &&
           (a->remote || memcmp(a->data, b->data, a->length) == 0);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);
    spi_inst_t *spi = MCP2518FD_TEST_SPI_INSTANCE == 0 ? spi0 : spi1;
    spi_init(spi, 1000000);
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MCP2518FD_TEST_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MCP2518FD_TEST_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MCP2518FD_TEST_MISO_PIN, GPIO_FUNC_SPI);
    const can_filter_t filter = {.id = 0x123, .mask = CAN_FLAG_EXTENDED | CAN_STANDARD_ID_MAX};
    const mcp2518fd_bus_config_t config = {
        .spi = spi,
        .cs_pin = MCP2518FD_TEST_CS_PIN,
        .int_pin = MCP2518FD_TEST_INT_PIN,
        .spi_baudrate_hz = MCP2518FD_TEST_SPI_BAUDRATE_HZ,
        .oscillator_hz = MCP2518FD_TEST_OSCILLATOR_HZ,
        .bitrate = MCP2518FD_TEST_BITRATE,
        .fd_enabled = MCP2518FD_TEST_FD,
        .data_bitrate = MCP2518FD_TEST_FD ? MCP2518FD_TEST_DATA_BITRATE : 0,
        .mode = MCP2518FD_TEST_MODE,
        .filters = MCP2518FD_TEST_FILTER ? &filter : NULL,
        .filter_count = MCP2518FD_TEST_FILTER ? 1 : 0,
    };
    mcp2518fd_result_t result = mcp2518fd_bus_init(&bus, &config);
    printf("MCP2518FD: %s; crystal=%lu nominal=%lu data=%lu mode=%u filter=%u\n",
           mcp2518fd_result_name(result), (unsigned long)config.oscillator_hz,
           (unsigned long)config.bitrate, (unsigned long)config.data_bitrate,
           config.mode, MCP2518FD_TEST_FILTER);
    if (result != MCP2518FD_OK) {
        while (true) tight_loop_contents();
    }

    uint32_t sequence = 0, passes = 0, failures = 0;
    uint64_t next_send = time_us_64(), next_stats = next_send + 5000000;
    uint64_t receive_deadline = 0;
    bool awaiting = false;
    can_fd_message_t expected = {0};
    while (true) {
        uint64_t now = time_us_64();
        if (now >= next_send && config.mode != MCP2518FD_MODE_LISTEN_ONLY) {
            can_fd_message_t message = test_message(sequence++);
            result = mcp2518fd_bus_send(&bus, &message);
            if (result == MCP2518FD_OK) {
                print_message("queued", &message);
                if (config.mode == MCP2518FD_MODE_LOOPBACK &&
                    can_filters_accept(config.filters, config.filter_count,
                        can_id_pack(message.id, message.extended, message.remote))) {
                    expected = message;
                    awaiting = true;
                    receive_deadline = now + 500000;
                }
            } else printf("send: %s\n", mcp2518fd_result_name(result));
            next_send = now + 1000000;
        }
        if (mcp2518fd_bus_interrupt_pending(&bus)) {
            /* Bound draining so sustained external traffic cannot starve diagnostics. */
            for (unsigned i = 0; i < MCP2518FD_RX_DEPTH; i++) {
                can_fd_message_t message;
                result = mcp2518fd_bus_receive(&bus, &message);
                if (result == MCP2518FD_ERR_RX_EMPTY) break;
                if (result != MCP2518FD_OK) {
                    printf("receive: %s\n", mcp2518fd_result_name(result));
                    break;
                }
                print_message("rx", &message);
                if (config.mode == MCP2518FD_MODE_LOOPBACK) {
                    if (awaiting && equal_message(&message, &expected)) passes++;
                    else failures++;
                    awaiting = false;
                }
            }
        }
        if (awaiting && now >= receive_deadline) {
            printf("loopback FAIL: receive timeout\n");
            failures++;
            awaiting = false;
        }
        if (now >= next_stats) {
            mcp2518fd_bus_stats_t stats;
            result = mcp2518fd_bus_get_stats(&bus, &stats);
            if (result == MCP2518FD_OK || result == MCP2518FD_ERR_RAM) {
                printf("stats queued=%lu rx=%lu overflow=%lu crc=%lu errors=%lu TREC=%08lx ECC=%08lx loopback_pass=%lu fail=%lu\n",
                    (unsigned long)stats.queued, (unsigned long)stats.received,
                    (unsigned long)stats.rx_overflow, (unsigned long)stats.spi_crc_errors,
                    (unsigned long)stats.controller_errors, (unsigned long)stats.trec,
                    (unsigned long)stats.eccstat, (unsigned long)passes, (unsigned long)failures);
            } else printf("stats: %s\n", mcp2518fd_result_name(result));
            next_stats = now + 5000000;
        }
        sleep_ms(1);
    }
}
