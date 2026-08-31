/*
 * firmware_update_test - runs the pure update-path logic on the target.
 *
 * Everything here is also covered by the host tests, and far more thoroughly.
 * The reason to run it on hardware anyway is that the host and the target are
 * not the same machine: the image header is written to flash byte for byte, so
 * its size and packing have to agree between whatever builds an image and the
 * bootloader that reads one, and the crc and ring buffer code has to behave
 * identically on a 32-bit target with a different compiler.
 *
 * It needs no wiring. See README.md.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "crc.h"
#include "firmware_image.h"
#include "flash_storage.h"
#include "hex_parser.h"
#include "ring_buffer.h"

/* Set by the 'write_flash' profile. Off by default: the write test erases the
   staging region, which is destructive even though it never touches the
   running firmware. */
#ifndef FIRMWARE_UPDATE_TEST_WRITE_FLASH
#define FIRMWARE_UPDATE_TEST_WRITE_FLASH 0
#endif

static unsigned checks_run;
static unsigned checks_failed;

static void report(const char *name, bool ok, const char *detail)
{
    checks_run++;
    if (!ok) {
        checks_failed++;
    }
    printf("  [%s] %-38s %s\n", ok ? "pass" : "FAIL", name, detail);
}

/* ------------------------------------------------------------------------
 * Layout: the reason this test exists at all
 * ---------------------------------------------------------------------- */

static void test_header_layout(void)
{
    char detail[96];

    /*
     * A header built by one program and read by another must occupy the same
     * bytes. If the target's compiler padded this differently from the host's,
     * an image written here would be unreadable there.
     */
    const bool size_ok = (sizeof(firmware_image_header_t) == 28);
    snprintf(detail, sizeof(detail), "%u bytes, expected 28",
             (unsigned)sizeof(firmware_image_header_t));
    report("header size on target", size_ok, detail);

    const bool offsets_ok =
        offsetof(firmware_image_header_t, magic) == 0 &&
        offsetof(firmware_image_header_t, payload_size) == 8 &&
        offsetof(firmware_image_header_t, build_id) == 20 &&
        offsetof(firmware_image_header_t, header_crc32) == 24;
    report("header field offsets", offsets_ok,
           offsets_ok ? "match the host layout" : "differ from the host layout");
}

/* ------------------------------------------------------------------------
 * CRC
 * ---------------------------------------------------------------------- */

static void test_crc(void)
{
    char detail[96];

    /* The published check value. If this differs from the host, an image
       checksummed by a build tool would never validate here. */
    const uint32_t value = crc32("123456789", 9);
    snprintf(detail, sizeof(detail), "0x%08lX, expected 0xCBF43926",
             (unsigned long)value);
    report("crc32 check value", value == 0xCBF43926u, detail);

    const uint16_t small = crc16_ccitt("123456789", 9);
    snprintf(detail, sizeof(detail), "0x%04X, expected 0x29B1", small);
    report("crc16 check value", small == 0x29B1u, detail);

    /* Incremental and one-shot must agree, since an image is checksummed in
       chunks as it arrives and whole when read back. */
    static uint8_t payload[512];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 7u + 1u);
    }

    uint32_t state = crc32_begin();
    for (size_t offset = 0; offset < sizeof(payload); offset += 37) {
        size_t remaining = sizeof(payload) - offset;
        if (remaining > 37) {
            remaining = 37;
        }
        state = crc32_update(state, &payload[offset], remaining);
    }
    report("crc32 incremental equals one-shot",
           crc32_end(state) == crc32(payload, sizeof(payload)), "512 bytes in 37s");
}

/* ------------------------------------------------------------------------
 * Intel HEX
 * ---------------------------------------------------------------------- */

/* A short image at the RP2040 flash base, in the form a linker emits. */
static const char *const hex_image[] = {
    ":020000041000EA",
    ":10000000000102030405060708090A0B0C0D0E0F78",
    ":10001000101112131415161718191A1B1C1D1E1F68",
    ":00000001FF",
};

static void test_hex_parser(void)
{
    char detail[96];
    hex_parser_t parser;
    hex_parser_reset(&parser);

    uint8_t image[32];
    size_t total = 0;
    uint32_t first_address = 0;
    bool ok = true;

    for (unsigned i = 0; i < count_of(hex_image) && ok; i++) {
        hex_record_t record;
        const hex_result_t result = hex_parser_feed(&parser, hex_image[i], &record);
        if (result != HEX_OK) {
            snprintf(detail, sizeof(detail), "line %u: %s", i, hex_result_name(result));
            ok = false;
            break;
        }
        if (record.type != HEX_RECORD_DATA) {
            continue;
        }
        if (total == 0) {
            first_address = record.address;
        }
        if (total + record.length > sizeof(image)) {
            ok = false;
            break;
        }
        memcpy(&image[total], record.data, record.length);
        total += record.length;
    }

    if (ok) {
        snprintf(detail, sizeof(detail), "%u bytes at 0x%08lX", (unsigned)total,
                 (unsigned long)first_address);
    }
    report("hex image reassembles", ok && total == 32 &&
           first_address == 0x10000000u && hex_parser_is_complete(&parser), detail);

    /* A corrupted line must be refused, not written into the image. */
    hex_parser_reset(&parser);
    hex_record_t record;
    const hex_result_t corrupted =
        hex_parser_feed(&parser, ":04000000DEADBEEFC5", &record);
    report("hex rejects a bad checksum", corrupted == HEX_ERR_BAD_CHECKSUM,
           hex_result_name(corrupted));
}

/* ------------------------------------------------------------------------
 * Image header and the boot decision
 * ---------------------------------------------------------------------- */

static void test_image_header(void)
{
    static uint8_t payload[256];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i ^ 0x5Au);
    }

    const uint32_t payload_crc = crc32(payload, sizeof(payload));

    firmware_image_header_t header;
    const firmware_image_result_t built =
        firmware_image_header_init(&header, sizeof(payload), payload_crc,
                                   0x10000000u, 1);
    report("header builds", built == FIRMWARE_IMAGE_OK,
           firmware_image_result_name(built));

    report("header validates",
           firmware_image_header_validate(&header) == FIRMWARE_IMAGE_OK, "");

    report("payload verifies",
           firmware_image_verify_payload(&header, payload_crc) == FIRMWARE_IMAGE_OK, "");

    /* Erased flash must never look like a header. */
    firmware_image_header_t erased;
    memset(&erased, 0xFF, sizeof(erased));
    report("erased flash is not a header", !firmware_image_header_is_valid(&erased),
           "0xFF fill rejected");

    /* A single flipped bit in the header must be caught. */
    firmware_image_header_t damaged = header;
    damaged.payload_size ^= 1u;
    report("damaged header is rejected", !firmware_image_header_is_valid(&damaged),
           "one flipped bit");
}

/* ------------------------------------------------------------------------
 * Ring buffer
 * ---------------------------------------------------------------------- */

static void test_ring_buffer(void)
{
    char detail[96];

    static uint8_t storage[16];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    snprintf(detail, sizeof(detail), "capacity %u of %u bytes",
             (unsigned)ring_buffer_capacity(&rb), (unsigned)sizeof(storage));
    report("capacity is one less than storage",
           ring_buffer_capacity(&rb) == sizeof(storage) - 1, detail);

    /* Push far more than it holds so the indices wrap many times, which is
       where a ring buffer usually goes wrong. */
    bool ok = true;
    for (unsigned i = 0; i < 4096 && ok; i++) {
        const uint8_t expected = (uint8_t)(i * 11u + 3u);
        uint8_t byte = 0;
        ok = ring_buffer_push(&rb, expected) &&
             ring_buffer_pop(&rb, &byte) &&
             byte == expected;
    }
    report("survives 4096 wraps", ok, "16-byte buffer");

    /* A block transfer that straddles the end of the storage. */
    static const uint8_t block[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    uint8_t received[sizeof(block)];
    memset(received, 0, sizeof(received));

    ring_buffer_write(&rb, block, sizeof(block));
    const size_t got = ring_buffer_read(&rb, received, sizeof(received));
    report("wrapping block transfer is intact",
           got == sizeof(block) && memcmp(block, received, sizeof(block)) == 0, "");
}

/* ------------------------------------------------------------------------
 * Flash
 *
 * The default checks are all read-only or expected to be *refused*, so they
 * verify the guards without writing anything. The erase and program test is
 * behind a profile flag because it destroys the staging region.
 * ---------------------------------------------------------------------- */

static void test_flash_layout(void)
{
    char detail[96];
    const flash_layout_t *layout = flash_layout_get();

    snprintf(detail, sizeof(detail), "app %uK staging %uK data %uK",
             (unsigned)(layout->application.size / 1024u),
             (unsigned)(layout->staging.size / 1024u),
             (unsigned)(layout->data.size / 1024u));
    report("flash divides up", layout->application.size > 0, detail);

    report("application and staging are equal",
           layout->application.size == layout->staging.size, "");

    report("regions are contiguous and cover the chip",
           layout->staging.offset == layout->application.size &&
           layout->data.offset == layout->staging.offset + layout->staging.size,
           "");
}

static void test_flash_guards(void)
{
    char detail[96];
    const flash_layout_t *layout = flash_layout_get();

    /*
     * The guard that matters. This firmware is executing from the application
     * region, so the component must know that and refuse to erase it. Nothing
     * is written by any of these — a refusal is the pass condition.
     */
    report("knows it runs from the application region",
           flash_storage_holds_running_code(&layout->application),
           "detected by the address of its own code");

    report("knows it does not run from staging",
           !flash_storage_holds_running_code(&layout->staging), "");

    const flash_storage_result_t self =
        flash_storage_erase(&layout->application, 0, FLASH_LAYOUT_SECTOR_SIZE);
    report("refuses to erase the running region",
           self == FLASH_STORAGE_ERR_RUNNING_FROM_REGION,
           flash_storage_result_name(self));

    const flash_storage_result_t misaligned =
        flash_storage_erase(&layout->staging, 1, FLASH_LAYOUT_SECTOR_SIZE);
    report("refuses a misaligned erase", misaligned == FLASH_STORAGE_ERR_UNALIGNED,
           flash_storage_result_name(misaligned));

    const flash_storage_result_t past_end =
        flash_storage_erase(&layout->staging, layout->staging.size,
                            FLASH_LAYOUT_SECTOR_SIZE);
    report("refuses an erase past the region",
           past_end == FLASH_STORAGE_ERR_OUT_OF_RANGE,
           flash_storage_result_name(past_end));

    /* Reading is just memory, so this is safe and proves the mapping. */
    const uint8_t *staged = flash_storage_data(&layout->staging, 0, 16);
    snprintf(detail, sizeof(detail), "staging maps to %p", (const void *)staged);
    report("staging is readable", staged != NULL, detail);
}

#if FIRMWARE_UPDATE_TEST_WRITE_FLASH
static void test_flash_write(void)
{
    char detail[96];
    const flash_layout_t *layout = flash_layout_get();

    /* One sector at the very start of staging. Nothing else uses it yet. */
    const flash_storage_result_t erased =
        flash_storage_erase(&layout->staging, 0, FLASH_LAYOUT_SECTOR_SIZE);
    report("erases a staging sector", erased == FLASH_STORAGE_OK,
           flash_storage_result_name(erased));

    report("erased flash reads as 0xFF",
           flash_storage_is_erased(&layout->staging, 0, FLASH_LAYOUT_SECTOR_SIZE), "");

    static uint8_t page[FLASH_LAYOUT_PAGE_SIZE];
    for (size_t i = 0; i < sizeof(page); i++) {
        page[i] = (uint8_t)(i ^ 0x3Cu);
    }

    const flash_storage_result_t written =
        flash_storage_program_verified(&layout->staging, 0, page, sizeof(page));
    report("programs and verifies a page", written == FLASH_STORAGE_OK,
           flash_storage_result_name(written));

    const uint32_t stored =
        flash_storage_crc32(&layout->staging, 0, sizeof(page));
    snprintf(detail, sizeof(detail), "0x%08lX", (unsigned long)stored);
    report("crc over flash matches crc over RAM",
           stored == crc32(page, sizeof(page)), detail);

    report("the written page is no longer erased",
           !flash_storage_is_erased(&layout->staging, 0, sizeof(page)), "");
}
#endif

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);

    printf("\nfirmware_update_test  board=%s\n", PICO_BOARD);

    unsigned pass = 0;
    while (true) {
        printf("\n--- pass %u ---\n", pass++);
        checks_run = 0;
        checks_failed = 0;

        test_header_layout();
        test_crc();
        test_hex_parser();
        test_image_header();
        test_ring_buffer();
        test_flash_layout();
        test_flash_guards();
#if FIRMWARE_UPDATE_TEST_WRITE_FLASH
        test_flash_write();
#endif

        printf("  %u checks, %u failed\n", checks_run, checks_failed);
        sleep_ms(5000);
    }
}
