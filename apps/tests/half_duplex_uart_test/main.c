/*
 * half_duplex_uart_test - hardware test for the half_duplex_uart component.
 *
 * The interesting property of a shared-wire bus is that the receiver hears the
 * transmitter. That makes this test self-contained: under
 * HALF_DUPLEX_UART_ECHO_KEEP everything sent comes straight back, so
 * the PIO programs, the clock divider and the pin handover can all be checked
 * end to end with nothing attached to the pin.
 *
 * See README.md beside this file.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "half_duplex_uart.h"

/* Overridable from the profiles under profiles/tests/half_duplex_uart_test. */
#ifndef HDX_TEST_PIN
#define HDX_TEST_PIN 21
#endif

#ifndef HDX_TEST_DIRECTION_PIN
#define HDX_TEST_DIRECTION_PIN HALF_DUPLEX_UART_NO_DIRECTION_PIN
#endif

#ifndef HDX_TEST_BAUDRATE
#define HDX_TEST_BAUDRATE 1000000
#endif

static half_duplex_uart_t bus;

static unsigned checks_run;
static unsigned checks_failed;

static void report(const char *name, bool ok, const char *detail)
{
    checks_run++;
    if (!ok) {
        checks_failed++;
    }
    printf("  [%s] %-34s %s\n", ok ? "pass" : "FAIL", name, detail);
}

static const char *result_name(half_duplex_uart_result_t r)
{
    switch (r) {
        case HALF_DUPLEX_UART_OK:                    return "ok";
        case HALF_DUPLEX_UART_ERR_INVALID_ARG:       return "invalid argument";
        case HALF_DUPLEX_UART_ERR_BAUDRATE:          return "baudrate unreachable";
        case HALF_DUPLEX_UART_ERR_NO_STATE_MACHINE:  return "no state machine";
        case HALF_DUPLEX_UART_ERR_NO_PROGRAM_SPACE:  return "no program space";
        case HALF_DUPLEX_UART_ERR_TIMEOUT:           return "timeout";
        case HALF_DUPLEX_UART_ERR_OVERFLOW:          return "overflow";
        default:                                     return "unknown";
    }
}

/*
 * Send a pattern and read it back off our own wire. Any mismatch means the
 * transmit and receive halves disagree about bit timing or framing.
 */
static void test_loopback(const char *name, const uint8_t *pattern, size_t len)
{
    uint8_t received[64];
    char detail[96];

    if (len > sizeof(received)) {
        report(name, false, "pattern longer than the receive buffer");
        return;
    }

    half_duplex_uart_flush_rx(&bus);

    const half_duplex_uart_result_t sent = half_duplex_uart_write(&bus, pattern, len);
    if (sent != HALF_DUPLEX_UART_OK) {
        snprintf(detail, sizeof(detail), "write failed: %s", result_name(sent));
        report(name, false, detail);
        return;
    }

    size_t count = 0;
    const half_duplex_uart_result_t got =
        half_duplex_uart_read(&bus, received, len, &count,
                              /* first byte  */ 5000,
                              /* inter-byte  */ 2000);

    if (got != HALF_DUPLEX_UART_OK || count != len) {
        snprintf(detail, sizeof(detail), "%s, got %u of %u bytes",
                 result_name(got), (unsigned)count, (unsigned)len);
        report(name, false, detail);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        if (received[i] != pattern[i]) {
            snprintf(detail, sizeof(detail),
                     "byte %u: sent 0x%02X, heard 0x%02X",
                     (unsigned)i, pattern[i], received[i]);
            report(name, false, detail);
            return;
        }
    }

    snprintf(detail, sizeof(detail), "%u bytes intact", (unsigned)len);
    report(name, true, detail);
}

static void test_patterns(void)
{
    /* All zeroes and all ones are the two patterns that break a UART whose bit
       timing is off: neither has a transition to resynchronise on. */
    static const uint8_t zeroes[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t ones[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t alternating[8] = { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA };
    static const uint8_t single[1] = { 0x5A };

    test_loopback("single byte", single, sizeof(single));
    test_loopback("all zero bits", zeroes, sizeof(zeroes));
    test_loopback("all one bits", ones, sizeof(ones));
    test_loopback("alternating bits", alternating, sizeof(alternating));

    /* Every possible byte, in chunks, to catch a bit position that is wrong
       only for some values. */
    uint8_t all[32];
    bool every_byte_ok = true;
    for (unsigned base = 0; base < 256 && every_byte_ok; base += sizeof(all)) {
        for (unsigned i = 0; i < sizeof(all); i++) {
            all[i] = (uint8_t)(base + i);
        }

        uint8_t received[sizeof(all)];
        size_t count = 0;

        half_duplex_uart_flush_rx(&bus);
        half_duplex_uart_write(&bus, all, sizeof(all));
        half_duplex_uart_read(&bus, received, sizeof(all), &count, 5000, 2000);

        if (count != sizeof(all) || memcmp(all, received, sizeof(all)) != 0) {
            every_byte_ok = false;
        }
    }
    report("all 256 byte values", every_byte_ok,
           every_byte_ok ? "0x00 to 0xFF intact" : "at least one value corrupted");
}

static void test_echo_suppression(void)
{
    /*
     * Under ECHO_DISCARD, write() is supposed to swallow the echo. On this wiring there is nothing else on the bus, so a following
     * read must find the line silent — if it returns data, suppression is off
     * by some number of bytes and every reply would be shifted.
     */
    half_duplex_uart_deinit(&bus);

    const half_duplex_uart_config_t config = {
        .pio = pio0,
        .pin = HDX_TEST_PIN,
        .direction_pin = HDX_TEST_DIRECTION_PIN,
        .baudrate = HDX_TEST_BAUDRATE,
        .echo = HALF_DUPLEX_UART_ECHO_DISCARD,
    };

    const half_duplex_uart_result_t init = half_duplex_uart_init(&bus, &config);
    if (init != HALF_DUPLEX_UART_OK) {
        report("echo suppression", false, result_name(init));
        return;
    }

    static const uint8_t pattern[6] = { 1, 2, 3, 4, 5, 6 };
    const half_duplex_uart_result_t sent =
        half_duplex_uart_write(&bus, pattern, sizeof(pattern));

    uint8_t leftover[8];
    size_t count = 0;
    half_duplex_uart_read(&bus, leftover, sizeof(leftover), &count, 2000, 1000);

    char detail[96];
    snprintf(detail, sizeof(detail), "write %s, %u stray bytes after",
             result_name(sent), (unsigned)count);
    report("echo suppression", sent == HALF_DUPLEX_UART_OK && count == 0, detail);
}

static void test_baudrates(void)
{
    static const uint32_t rates[] = { 57600, 115200, 250000, 500000, 1000000 };
    static const uint8_t pattern[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    for (unsigned i = 0; i < count_of(rates); i++) {
        char name[48];
        char detail[96];

        const half_duplex_uart_result_t set =
            half_duplex_uart_set_baudrate(&bus, rates[i]);
        snprintf(name, sizeof(name), "loopback at %lu baud", (unsigned long)rates[i]);

        if (set != HALF_DUPLEX_UART_OK) {
            report(name, false, result_name(set));
            continue;
        }

        const half_duplex_uart_timing_t *timing = half_duplex_uart_get_timing(&bus);

        uint8_t received[sizeof(pattern)];
        size_t count = 0;

        half_duplex_uart_flush_rx(&bus);
        half_duplex_uart_write(&bus, pattern, sizeof(pattern));
        half_duplex_uart_read(&bus, received, sizeof(pattern), &count, 20000, 10000);

        const bool ok = (count == sizeof(pattern)) &&
                        memcmp(pattern, received, sizeof(pattern)) == 0;
        snprintf(detail, sizeof(detail), "actual %lu baud, error %ld/1000",
                 (unsigned long)timing->actual_baudrate,
                 (long)timing->error_permille);
        report(name, ok, detail);
    }

    half_duplex_uart_set_baudrate(&bus, HDX_TEST_BAUDRATE);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    printf("\nhalf_duplex_uart_test  board=%s pin=%d dir=%d baud=%d\n",
           PICO_BOARD, HDX_TEST_PIN, HDX_TEST_DIRECTION_PIN, HDX_TEST_BAUDRATE);

    unsigned pass = 0;
    while (true) {
        printf("\n--- pass %u ---\n", pass++);
        checks_run = 0;
        checks_failed = 0;

        /* Echo left on so the loopback tests can see our own bytes. */
        const half_duplex_uart_config_t config = {
            .pio = pio0,
            .pin = HDX_TEST_PIN,
            .direction_pin = HDX_TEST_DIRECTION_PIN,
            .baudrate = HDX_TEST_BAUDRATE,
            /* Keep the echo: on a shared pad that turns the bus into a
               loopback, which is what makes this test need no wiring. */
            .echo = HALF_DUPLEX_UART_ECHO_KEEP,
        };

        const half_duplex_uart_result_t init = half_duplex_uart_init(&bus, &config);
        if (init != HALF_DUPLEX_UART_OK) {
            printf("  init failed: %s\n", result_name(init));
            sleep_ms(2000);
            continue;
        }

        const half_duplex_uart_timing_t *timing = half_duplex_uart_get_timing(&bus);
        printf("  timing: divider %u+%u/256, actual %lu baud, error %ld/1000\n",
               timing->divider_int, timing->divider_frac,
               (unsigned long)timing->actual_baudrate,
               (long)timing->error_permille);

        test_patterns();
        test_baudrates();
        test_echo_suppression();

        half_duplex_uart_deinit(&bus);

        printf("  %u checks, %u failed\n", checks_run, checks_failed);
        sleep_ms(3000);
    }
}
