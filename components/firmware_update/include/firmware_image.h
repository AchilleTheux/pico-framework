/*
 * firmware_image - the on-flash description of a firmware image.
 *
 * Written to the manifest sector when a transfer has been verified, and read
 * back at startup, so an image can be uploaded, the board power-cycled, and
 * the image installed afterwards without sending it again.
 *
 * An update over a serial link can be interrupted at any point: the cable is
 * pulled, the battery sags, the sender crashes halfway. Everything here exists
 * so that a half-written image is recognisably half-written rather than
 * something the board tries to install.
 *
 * The layout is deliberately plain — fixed-width fields, no padding, one CRC
 * over the header and another over the payload — because it is written by one
 * program and read by another, possibly a different build.
 *
 * No Pico SDK dependency: this is the part that can be got right before any
 * flash is involved, and it is unit-tested on the host.
 */

#ifndef PICO_FRAMEWORK_FIRMWARE_IMAGE_H
#define PICO_FRAMEWORK_FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "PFW1" as little-endian bytes. Chosen so neither erased flash (0xFFFFFFFF)
   nor zeroed flash (0x00000000) can be mistaken for a header. */
#define FIRMWARE_IMAGE_MAGIC 0x31574650u

#define FIRMWARE_IMAGE_HEADER_VERSION 1u

/*
 * An image larger than this is rejected before anything is erased. It is a
 * sanity bound, not a slot size: the slot's own capacity is checked separately
 * by whoever owns the flash.
 */
#ifndef FIRMWARE_IMAGE_MAX_PAYLOAD
#define FIRMWARE_IMAGE_MAX_PAYLOAD (8u * 1024u * 1024u)
#endif

/*
 * Written to flash verbatim, so the layout is part of the contract between the
 * session that verifies an image and the later session that recovers it.
 * Fields are ordered to need no padding, and a static assertion below fixes
 * the size.
 */
typedef struct {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t load_address;

    /*
     * Identifies the staged image. The updater sets it to the payload
     * checksum, which is as good an identity as anything available and needs
     * nothing from the sender.
     */
    uint32_t build_id;

    /* Covers every field above. Lets a torn header be told from a good one
       before the payload is even looked at. */
    uint32_t header_crc32;
} firmware_image_header_t;

_Static_assert(sizeof(firmware_image_header_t) == 28,
               "the header layout is written to flash and must not change size");

typedef enum {
    FIRMWARE_IMAGE_OK = 0,
    FIRMWARE_IMAGE_ERR_INVALID_ARG,
    FIRMWARE_IMAGE_ERR_BAD_MAGIC,      /* blank, erased, or not a header */
    FIRMWARE_IMAGE_ERR_BAD_VERSION,    /* a header format we do not know */
    FIRMWARE_IMAGE_ERR_BAD_SIZE,       /* empty, or beyond the sanity bound */
    FIRMWARE_IMAGE_ERR_BAD_HEADER_CRC, /* the header itself is damaged */
    FIRMWARE_IMAGE_ERR_BAD_PAYLOAD_CRC,
} firmware_image_result_t;

/*
 * Fill in a header for a payload, computing the header CRC. `payload_crc32` is
 * the caller's, from the crc component, over exactly `payload_size` bytes.
 */
firmware_image_result_t firmware_image_header_init(firmware_image_header_t *header,
                                                   uint32_t payload_size,
                                                   uint32_t payload_crc32,
                                                   uint32_t load_address,
                                                   uint32_t build_id);

/*
 * Check a header found in flash: magic, version, declared size, and its own
 * CRC. Says nothing about the payload, which may not have been written yet.
 */
firmware_image_result_t firmware_image_header_validate(const firmware_image_header_t *header);

static inline bool firmware_image_header_is_valid(const firmware_image_header_t *header)
{
    return firmware_image_header_validate(header) == FIRMWARE_IMAGE_OK;
}

/*
 * Check a payload CRC the caller computed against the one the header claims.
 * Separate from the header check because the payload is megabytes and is
 * streamed, while the header is 28 bytes and is checked constantly.
 */
firmware_image_result_t firmware_image_verify_payload(const firmware_image_header_t *header,
                                                      uint32_t payload_crc32);

/* The CRC a well-formed header would carry. Exposed for tests and tooling. */
uint32_t firmware_image_header_crc(const firmware_image_header_t *header);

const char *firmware_image_result_name(firmware_image_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FIRMWARE_IMAGE_H */
