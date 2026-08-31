/*
 * firmware_receive - assembling an incoming firmware image into flash pages.
 *
 * Sits between hex_parser, which turns a line into an address and some bytes,
 * and flash_storage, which writes whole 256-byte pages. What it does is the
 * bookkeeping between those two: map an absolute address to an offset in the
 * staging region, gather bytes into a page, flush a page when the next record
 * lands outside it, and refuse anything that would fall outside the region.
 *
 * Pages are handed to a caller-supplied writer rather than to flash directly,
 * so all of this is exercised on the host against an array. That matters more
 * than it sounds: the interesting cases are records that straddle a page
 * boundary, records that arrive out of order, and images with gaps, none of
 * which are convenient to provoke on hardware.
 *
 * No Pico SDK dependency.
 */

#ifndef PICO_FRAMEWORK_FIRMWARE_RECEIVE_H
#define PICO_FRAMEWORK_FIRMWARE_RECEIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hex_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flash programs a page at a time, so that is the unit assembled here. */
#define FIRMWARE_RECEIVE_PAGE_SIZE 256u

typedef enum {
    FIRMWARE_RECEIVE_OK = 0,
    FIRMWARE_RECEIVE_COMPLETE,        /* the end-of-file record was accepted */
    FIRMWARE_RECEIVE_ERR_INVALID_ARG,
    FIRMWARE_RECEIVE_ERR_NOT_STARTED,
    FIRMWARE_RECEIVE_ERR_HEX,         /* the line was not a valid record */
    FIRMWARE_RECEIVE_ERR_BELOW_BASE,  /* addressed before the image start */
    FIRMWARE_RECEIVE_ERR_TOO_LARGE,   /* would not fit in the region */
    FIRMWARE_RECEIVE_ERR_WRITE,       /* the writer refused a page */
    FIRMWARE_RECEIVE_ERR_ABORTED,     /* an earlier record failed */
} firmware_receive_result_t;

/*
 * Called with one page to store. `offset` is relative to the region and is
 * always a multiple of FIRMWARE_RECEIVE_PAGE_SIZE. Return false to fail the
 * transfer.
 */
typedef bool (*firmware_page_writer_fn)(void *ctx, uint32_t offset,
                                        const uint8_t *page, uint32_t size);

typedef struct {
    /*
     * The absolute address that corresponds to offset 0. For an image linked
     * to run from the start of flash this is XIP_BASE, 0x10000000 — the
     * address the linker put in the HEX file, not where it is staged.
     */
    uint32_t base_address;

    /* Bytes available in the destination region. */
    uint32_t capacity;

    firmware_page_writer_fn write_page;
    void *write_ctx;
} firmware_receive_config_t;

typedef struct {
    uint32_t base_address;
    uint32_t capacity;
    firmware_page_writer_fn write_page;
    void *write_ctx;

    hex_parser_t parser;

    uint8_t page[FIRMWARE_RECEIVE_PAGE_SIZE];
    uint32_t page_offset;   /* region offset of page[0] */
    bool page_dirty;

    uint32_t records;
    uint32_t bytes;         /* data bytes accepted, gaps not counted */
    uint32_t image_size;    /* one past the highest offset written */
    uint32_t pages_written;

    bool started;
    bool complete;
    firmware_receive_result_t error;
} firmware_receive_t;

/* Discard any previous transfer and prepare for a new one. Does not erase
   anything: that is the caller's to do before the first page arrives. */
firmware_receive_result_t firmware_receive_begin(firmware_receive_t *rx,
                                                 const firmware_receive_config_t *config);

/*
 * Feed one line, as read from the link.
 *
 * Returns FIRMWARE_RECEIVE_COMPLETE for the end-of-file record and
 * FIRMWARE_RECEIVE_OK for anything else it accepted. Once a line fails, the
 * transfer stays failed and every later line returns
 * FIRMWARE_RECEIVE_ERR_ABORTED — a partly written image must not be able to
 * look finished.
 */
firmware_receive_result_t firmware_receive_line(firmware_receive_t *rx, const char *line);

/*
 * Flush whatever is left in the part-filled page. Called automatically for the
 * end-of-file record; call it directly only when ending a transfer early.
 */
firmware_receive_result_t firmware_receive_flush(firmware_receive_t *rx);

/* True when the end-of-file record has been accepted and everything written. */
static inline bool firmware_receive_is_complete(const firmware_receive_t *rx)
{
    return rx->complete;
}

/*
 * Bytes from offset 0 up to the highest one written, gaps included. This is
 * the span to checksum and the span to install — not `bytes`, which counts
 * only what arrived.
 */
static inline uint32_t firmware_receive_image_size(const firmware_receive_t *rx)
{
    return rx->image_size;
}

const char *firmware_receive_result_name(firmware_receive_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FIRMWARE_RECEIVE_H */
