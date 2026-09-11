/* A small SPI register model checks transaction boundaries, addressing and
   failure paths. It does not simulate CAN arbitration or electrical timing. */
#include "test.h"
#include "hardware/gpio.h"
#include "mcp2518fd.h"

spi_inst_t test_spi[2];
static uint8_t memory[4096], header[3];
static bool selected, have_header, data_read, pins[30], absent, stuck_mode;
static unsigned bad_crc, tx_count, rx_count, rx_head, rx_tail, ram_reads;
static uint16_t address, read_crc;
static uint64_t waited;
static bool ecc_on_ram_read;

static void set_word(unsigned reg, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) memory[reg + i] = value >> (8 * i);
}

static uint32_t word(unsigned reg)
{
    return (uint32_t)memory[reg] | ((uint32_t)memory[reg+1] << 8) |
           ((uint32_t)memory[reg+2] << 16) | ((uint32_t)memory[reg+3] << 24);
}

static void reset_chip(void)
{
    memset(memory, 0, sizeof(memory));
    set_word(0, 0x04980760); /* CiCON POR, including TEF and TXQ enabled. */
    set_word(0xe00, 0x400);
    tx_count = rx_count = rx_head = rx_tail = 0;
}

static void setup(void)
{
    reset_chip();
    memset(pins, 0, sizeof(pins));
    selected = have_header = data_read = absent = stuck_mode = ecc_on_ram_read = false;
    bad_crc = ram_reads = 0;
    waited = 0;
    test_spi[0].baudrate = test_spi[1].baudrate = 1000000;
}

uint spi_set_baudrate(spi_inst_t *spi, uint baudrate) { return spi->baudrate = baudrate; }
uint spi_get_baudrate(const spi_inst_t *spi) { return spi->baudrate; }
void gpio_init(uint pin) { CHECK(pin < 30); pins[pin] = true; }
void gpio_deinit(uint pin) { CHECK(pin < 30); pins[pin] = false; }
void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
void gpio_pull_up(uint pin) { (void)pin; }
bool gpio_get(uint pin) { (void)pin; return rx_count == 0; }
void sleep_us(uint64_t us) { waited += us; }

void gpio_put(uint pin, bool value)
{
    (void)pin;
    if (!value) {
        CHECK(!selected);
        selected = true;
        have_header = data_read = false;
    } else selected = false;
}

static void sync_fifos(void)
{
    set_word(0x60, tx_count < 4 ? 1 : 0);
    set_word(0x64, (tx_count % 4) * 72);
    memory[0x6c] = (memory[0x6c] & 8) | (rx_count ? 1 : 0);
    set_word(0x70, 4 * 72 + rx_tail * 72);
}

int spi_write_blocking(spi_inst_t *spi, const uint8_t *data, size_t length)
{
    (void)spi;
    CHECK(selected);
    if (!have_header) {
        CHECK(length == 2 || length == 3);
        if (length != 2 && length != 3) return length;
        memcpy(header, data, length);
        have_header = true;
        address = ((data[0] & 15) << 8) | data[1];
        if ((data[0] >> 4) == 0) {
            CHECK_EQ_U32((word(0) >> 21) & 7, 4); /* config before RESET */
            if (!absent) reset_chip();
        } else CHECK((data[0] >> 4) == 2 || (data[0] >> 4) == 11);
        return length;
    }
    CHECK_EQ_U32(header[0] >> 4, 2);
    CHECK(address + length <= sizeof(memory));
    if (absent) return length;
    if (address >= 0x400 && address < 0xc00) {
        CHECK_EQ_U32(address % 4, 0);
        CHECK_EQ_U32(length % 4, 0);
    }
    if (address == 0x6c || address == 0xe10 || address == 0x1d) memory[address] &= data[0];
    else memcpy(memory + address, data, length);
    if (address == 3 && !stuck_mode) {
        uint32_t mode = data[0] & 7;
        set_word(0, (word(0) & ~(7u << 21)) | (mode << 21));
        if (mode == 4) tx_count = rx_count = rx_head = rx_tail = 0;
        sync_fifos();
    }
    if (address == 0xe00) set_word(0xe00, data[0] | 0x400 | (data[0] & 1 ? 0x100 : 0));
    if (address == 0x5d && (data[0] & 1)) {
        CHECK_EQ_U32(data[0], 3);
        CHECK(tx_count < 4);
        tx_count++;
        sync_fifos();
    }
    if (address == 0x69 && (data[0] & 1)) {
        CHECK(rx_count > 0);
        rx_count--;
        rx_tail = (rx_tail + 1) % 16;
        sync_fifos();
    }
    return length;
}

int spi_read_blocking(spi_inst_t *spi, uint8_t repeated, uint8_t *data, size_t length)
{
    (void)spi; (void)repeated;
    CHECK(selected && have_header);
    CHECK_EQ_U32(header[0] >> 4, 11); /* no unchecked READ commands */
    if (!data_read) {
        bool ram = address >= 0x400 && address < 0xc00;
        CHECK_EQ_U32(header[2], ram ? length / 4 : length);
        CHECK(address + length <= sizeof(memory));
        if (ram) {
            CHECK(rx_count != 0); /* no RAM read before nonempty status */
            ram_reads++;
            if (ecc_on_ram_read) set_word(0xe10, 0x05000002);
        }
        if (absent) memset(data, 0xff, length);
        else memcpy(data, memory + address, length);
        read_crc = mcp2518fd_crc16(0xffff, header, 3);
        read_crc = mcp2518fd_crc16(read_crc, data, length);
        data_read = true;
    } else {
        CHECK_EQ_U32(length, 2);
        if (bad_crc || absent) {
            read_crc ^= 1;
            if (bad_crc) bad_crc--;
        }
        data[0] = read_crc >> 8;
        data[1] = read_crc;
    }
    return length;
}

static mcp2518fd_bus_config_t config(void)
{
    return (mcp2518fd_bus_config_t){.spi = spi1, .cs_pin = 9, .int_pin = 8,
        .oscillator_hz = 40000000, .fd_enabled = true, .mode = MCP2518FD_MODE_LOOPBACK};
}

static void inject_frame(const can_fd_message_t *message)
{
    CHECK(rx_count < 16);
    CHECK(mcp2518fd_frame_pack(message, memory + 0x400 + 4 * 72 + rx_head * 72));
    rx_head = (rx_head + 1) % 16;
    rx_count++;
    sync_fifos();
}

TEST(init_and_fifo_io)
{
    setup();
    mcp2518fd_bus_t bus;
    mcp2518fd_bus_config_t cfg = config();
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    CHECK_EQ_U32(word(4), 0x003e0f0f);
    CHECK_EQ_U32(word(8), 0x000e0303);
    CHECK_EQ_U32(word(0x5c), 0xe3000080);
    CHECK_EQ_U32(word(0x68), 0xef000009);
    CHECK_EQ_U32(memory[0x1d0], 0x82);
    CHECK_EQ_U32(word(0x1f4), 0); /* no filters accepts standard and extended */
    CHECK_EQ_U32(word(0) & (3u << 19), 0); /* no TEF/TXQ */
    CHECK(!mcp2518fd_bus_interrupt_pending(&bus));
    can_fd_message_t message = {.id = 0x123, .fd = true, .length = 64, .data = {0xaa}};
    CHECK_EQ_INT(mcp2518fd_bus_receive(&bus, &message), MCP2518FD_ERR_RX_EMPTY);
    CHECK_EQ_U32(ram_reads, 0);
    for (unsigned i = 0; i < 4; i++) {
        CHECK(mcp2518fd_bus_can_send(&bus));
        CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_OK);
        CHECK_EQ_U32(memory[0x408 + i * 72], 0xaa);
    }
    CHECK(!mcp2518fd_bus_can_send(&bus));
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_TX_FULL);
    /* Receive more than a FIFO's depth to cover UA wrapping. */
    for (unsigned i = 0; i < 40; i++) {
        message.id = i;
        inject_frame(&message);
        CHECK(mcp2518fd_bus_interrupt_pending(&bus));
        can_fd_message_t rx;
        CHECK_EQ_INT(mcp2518fd_bus_receive(&bus, &rx), MCP2518FD_OK);
        CHECK_EQ_U32(rx.id, i);
        CHECK_EQ_U32(rx.length, 64);
        CHECK_EQ_U32(rx.data[0], 0xaa);
    }
    mcp2518fd_bus_deinit(&bus);
    CHECK(!bus.initialised && !pins[8] && !pins[9]);
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_NOT_INITIALISED);
}

TEST(crc_and_ram_failures)
{
    setup();
    mcp2518fd_bus_t bus;
    mcp2518fd_bus_config_t cfg = config();
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    can_fd_message_t message = {.id = 0x321, .fd = true, .length = 12};
    inject_frame(&message);
    bad_crc = 2;
    CHECK_EQ_INT(mcp2518fd_bus_receive(&bus, &message), MCP2518FD_OK);
    CHECK_EQ_U32(bus.stats.spi_crc_errors, 2);
    inject_frame(&message);
    bad_crc = 3;
    message.id = 99;
    CHECK_EQ_INT(mcp2518fd_bus_receive(&bus, &message), MCP2518FD_ERR_SPI);
    CHECK_EQ_U32(message.id, 99);
    CHECK_EQ_U32(rx_count, 1); /* failed read never consumes a message */
    ecc_on_ram_read = true;
    CHECK_EQ_INT(mcp2518fd_bus_receive(&bus, &message), MCP2518FD_ERR_RAM);
    CHECK_EQ_U32(message.id, 99);
    CHECK_EQ_U32(rx_count, 1);
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_RAM);
    mcp2518fd_bus_stats_t stats;
    CHECK_EQ_INT(mcp2518fd_bus_get_stats(&bus, &stats), MCP2518FD_ERR_RAM);
    CHECK_EQ_U32(stats.eccstat, 0x05000002);
    mcp2518fd_bus_deinit(&bus);

    setup();
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    set_word(0x64, 0xffffffff);
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_RAM);
    CHECK_EQ_U32(tx_count, 0);
    mcp2518fd_bus_deinit(&bus);
}

TEST(init_failures_and_modes)
{
    setup();
    mcp2518fd_bus_t bus = {0};
    mcp2518fd_bus_config_t cfg = config();
    cfg.oscillator_hz = 16000000;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_ERR_INVALID_ARG);
    CHECK(!pins[9]);
    cfg = config();
    cfg.int_pin = 30;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_ERR_INVALID_ARG);
    cfg = config();
    cfg.spi_baudrate_hz = 17000001;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_ERR_INVALID_ARG);
    cfg = config();
    absent = true;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_ERR_NO_DEVICE);
    CHECK(!pins[9] && !pins[8] && !bus.initialised);
    setup();
    cfg.oscillator_hz = 4000000;
    cfg.spi_baudrate_hz = 10000000;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    CHECK_EQ_U32(word(0xe00), 0x501); /* PLL enabled and ready */
    CHECK_EQ_U32(spi_get_baudrate(spi1), 10000000);
    CHECK_EQ_INT(mcp2518fd_bus_set_mode(&bus, MCP2518FD_MODE_LISTEN_ONLY), MCP2518FD_OK);
    can_fd_message_t message = {.fd = true};
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_MONITOR_MODE);
    CHECK(!mcp2518fd_bus_can_send(&bus));
    stuck_mode = true;
    CHECK_EQ_INT(mcp2518fd_bus_set_mode(&bus, MCP2518FD_MODE_NORMAL), MCP2518FD_ERR_TIMEOUT);
    CHECK(!bus.initialised && !pins[9]);
    CHECK(waited < 100000); /* all polling is bounded */

    setup();
    cfg = config();
    cfg.fd_enabled = false;
    cfg.mode = MCP2518FD_MODE_NORMAL;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    CHECK_EQ_U32((word(0) >> 21) & 7, 6);
    CHECK_EQ_INT(mcp2518fd_bus_send(&bus, &message), MCP2518FD_ERR_INVALID_ARG);
    mcp2518fd_bus_deinit(&bus);
}

TEST(filters_and_diagnostics)
{
    setup();
    mcp2518fd_bus_t bus;
    mcp2518fd_bus_config_t cfg = config();
    can_filter_t filters[32];
    for (unsigned i = 0; i < 32; i++) filters[i] = (can_filter_t){.id = i, .mask = CAN_FLAG_EXTENDED | 0x7ff};
    cfg.filters = filters;
    cfg.filter_count = 32;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_OK);
    CHECK_EQ_U32(memory[0x1ef], 0x82); /* last filter enabled */
    CHECK_EQ_U32(word(0x2e8), 31);
    CHECK_EQ_U32(word(0x2ec), 0x400007ff);
    memory[0x6c] |= 8;
    memory[0x1d] |= 0x30;
    set_word(0x34, 0x200123);
    mcp2518fd_bus_stats_t stats;
    CHECK_EQ_INT(mcp2518fd_bus_get_stats(&bus, &stats), MCP2518FD_OK);
    CHECK_EQ_U32(stats.rx_overflow, 1);
    CHECK_EQ_U32(stats.controller_errors, 1);
    CHECK_EQ_U32(stats.trec, 0x200123);
    CHECK_EQ_INT(mcp2518fd_bus_get_stats(&bus, &stats), MCP2518FD_OK);
    CHECK_EQ_U32(stats.rx_overflow, 1);
    CHECK_EQ_U32(stats.controller_errors, 1);
    mcp2518fd_bus_deinit(&bus);
    cfg.filter_count = 33;
    CHECK_EQ_INT(mcp2518fd_bus_init(&bus, &cfg), MCP2518FD_ERR_INVALID_ARG);
}

TEST_MAIN(RUN(init_and_fifo_io); RUN(crc_and_ram_failures);
          RUN(init_failures_and_modes); RUN(filters_and_diagnostics);)
