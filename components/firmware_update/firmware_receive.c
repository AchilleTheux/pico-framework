#include <string.h>

#include "firmware_receive.h"

/* What erased flash reads as. A page is filled with this before any record
   goes into it, so that a gap in the image is left erased rather than
   programmed to zero — programming can only clear bits, so zeroing a gap
   would be permanent and would corrupt anything written there later. */
#define ERASED_BYTE 0xFFu

static void reset_page(firmware_receive_t *rx, uint32_t page_offset)
{
    memset(rx->page, ERASED_BYTE, sizeof(rx->page));
    rx->page_offset = page_offset;
    rx->page_dirty = false;
}

firmware_receive_result_t firmware_receive_begin(firmware_receive_t *rx,
                                                 const firmware_receive_config_t *config)
{
    if (rx == NULL || config == NULL || config->write_page == NULL ||
        config->capacity == 0) {
        return FIRMWARE_RECEIVE_ERR_INVALID_ARG;
    }

    memset(rx, 0, sizeof(*rx));
    rx->base_address = config->base_address;
    rx->capacity = config->capacity;
    rx->write_page = config->write_page;
    rx->write_ctx = config->write_ctx;
    rx->started = true;

    hex_parser_reset(&rx->parser);
    reset_page(rx, 0);

    return FIRMWARE_RECEIVE_OK;
}

/* Hand the current page to the writer, if anything has been put in it. */
static firmware_receive_result_t flush_page(firmware_receive_t *rx)
{
    if (!rx->page_dirty) {
        return FIRMWARE_RECEIVE_OK;
    }

    if (!rx->write_page(rx->write_ctx, rx->page_offset, rx->page, sizeof(rx->page))) {
        return FIRMWARE_RECEIVE_ERR_WRITE;
    }

    rx->pages_written++;
    rx->page_dirty = false;
    return FIRMWARE_RECEIVE_OK;
}

/*
 * Place one byte.
 *
 * Written a byte at a time rather than in runs because records are at most 255
 * bytes and the boundary cases — a record straddling a page, a record landing
 * in a page already started, a record jumping backwards — are the ones that go
 * wrong. Doing it uniformly costs nothing at these sizes and removes the
 * special cases entirely.
 */
static firmware_receive_result_t place_byte(firmware_receive_t *rx,
                                            uint32_t address, uint8_t value)
{
    if (address < rx->base_address) {
        return FIRMWARE_RECEIVE_ERR_BELOW_BASE;
    }

    const uint32_t offset = address - rx->base_address;
    if (offset >= rx->capacity) {
        return FIRMWARE_RECEIVE_ERR_TOO_LARGE;
    }

    const uint32_t page_offset = offset - (offset % FIRMWARE_RECEIVE_PAGE_SIZE);

    /* Moving to a different page means the current one is finished, whether
       the image is contiguous or jumps about. */
    if (rx->page_dirty && page_offset != rx->page_offset) {
        const firmware_receive_result_t flushed = flush_page(rx);
        if (flushed != FIRMWARE_RECEIVE_OK) {
            return flushed;
        }
    }
    if (!rx->page_dirty) {
        reset_page(rx, page_offset);
    }

    rx->page[offset % FIRMWARE_RECEIVE_PAGE_SIZE] = value;
    rx->page_dirty = true;
    rx->bytes++;

    if (offset + 1u > rx->image_size) {
        rx->image_size = offset + 1u;
    }

    return FIRMWARE_RECEIVE_OK;
}

firmware_receive_result_t firmware_receive_flush(firmware_receive_t *rx)
{
    if (rx == NULL) {
        return FIRMWARE_RECEIVE_ERR_INVALID_ARG;
    }
    if (!rx->started) {
        return FIRMWARE_RECEIVE_ERR_NOT_STARTED;
    }
    if (rx->error != FIRMWARE_RECEIVE_OK) {
        return FIRMWARE_RECEIVE_ERR_ABORTED;
    }

    const firmware_receive_result_t flushed = flush_page(rx);
    if (flushed != FIRMWARE_RECEIVE_OK) {
        rx->error = flushed;
    }
    return flushed;
}

firmware_receive_result_t firmware_receive_line(firmware_receive_t *rx, const char *line)
{
    if (rx == NULL || line == NULL) {
        return FIRMWARE_RECEIVE_ERR_INVALID_ARG;
    }
    if (!rx->started) {
        return FIRMWARE_RECEIVE_ERR_NOT_STARTED;
    }

    /* One bad record poisons the transfer. Carrying on would leave an image
       with a hole in it that still reached the end-of-file record and so
       looked complete. */
    if (rx->error != FIRMWARE_RECEIVE_OK) {
        return FIRMWARE_RECEIVE_ERR_ABORTED;
    }

    hex_record_t record;
    if (hex_parser_feed(&rx->parser, line, &record) != HEX_OK) {
        rx->error = FIRMWARE_RECEIVE_ERR_HEX;
        return rx->error;
    }

    rx->records++;

    switch (record.type) {
        case HEX_RECORD_DATA:
            for (uint8_t i = 0; i < record.length; i++) {
                const firmware_receive_result_t placed =
                    place_byte(rx, record.address + i, record.data[i]);
                if (placed != FIRMWARE_RECEIVE_OK) {
                    rx->error = placed;
                    return placed;
                }
            }
            return FIRMWARE_RECEIVE_OK;

        case HEX_RECORD_EOF: {
            const firmware_receive_result_t flushed = flush_page(rx);
            if (flushed != FIRMWARE_RECEIVE_OK) {
                rx->error = flushed;
                return flushed;
            }
            rx->complete = true;
            return FIRMWARE_RECEIVE_COMPLETE;
        }

        default:
            /* Address extension and start-address records carry no image data;
               hex_parser has already applied them. */
            return FIRMWARE_RECEIVE_OK;
    }
}

const char *firmware_receive_result_name(firmware_receive_result_t result)
{
    switch (result) {
        case FIRMWARE_RECEIVE_OK:               return "ok";
        case FIRMWARE_RECEIVE_COMPLETE:         return "complete";
        case FIRMWARE_RECEIVE_ERR_INVALID_ARG:  return "invalid argument";
        case FIRMWARE_RECEIVE_ERR_NOT_STARTED:  return "no transfer started";
        case FIRMWARE_RECEIVE_ERR_HEX:          return "bad hex record";
        case FIRMWARE_RECEIVE_ERR_BELOW_BASE:   return "address below the image base";
        case FIRMWARE_RECEIVE_ERR_TOO_LARGE:    return "image larger than the region";
        case FIRMWARE_RECEIVE_ERR_WRITE:        return "flash write failed";
        case FIRMWARE_RECEIVE_ERR_ABORTED:      return "transfer already failed";
        default:                                return "unknown";
    }
}
