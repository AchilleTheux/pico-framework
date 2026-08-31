#include <string.h>

#include "crc.h"

#include "firmware_image.h"

/* The header CRC covers everything before the CRC field itself. */
#define HEADER_CRC_COVERAGE (sizeof(firmware_image_header_t) - sizeof(uint32_t))

uint32_t firmware_image_header_crc(const firmware_image_header_t *header)
{
    if (header == NULL) {
        return 0;
    }
    return crc32(header, HEADER_CRC_COVERAGE);
}

firmware_image_result_t firmware_image_header_init(firmware_image_header_t *header,
                                                   uint32_t payload_size,
                                                   uint32_t payload_crc32,
                                                   uint32_t load_address,
                                                   uint32_t build_id)
{
    if (header == NULL) {
        return FIRMWARE_IMAGE_ERR_INVALID_ARG;
    }
    if (payload_size == 0 || payload_size > FIRMWARE_IMAGE_MAX_PAYLOAD) {
        return FIRMWARE_IMAGE_ERR_BAD_SIZE;
    }

    /*
     * Zero the whole struct first. Today this is unobservable: the fields
     * happen to pack with no padding, as the size assertion in the header
     * pins, and every one of them is assigned below. It is here for the
     * version of this struct that gains a uint8_t field and therefore gains
     * padding — at which point undefined bytes would be written to flash and
     * two builds of the same image would stop being byte-identical.
     */
    memset(header, 0, sizeof(*header));

    header->magic = FIRMWARE_IMAGE_MAGIC;
    header->header_version = FIRMWARE_IMAGE_HEADER_VERSION;
    header->header_size = (uint16_t)sizeof(firmware_image_header_t);
    header->payload_size = payload_size;
    header->payload_crc32 = payload_crc32;
    header->load_address = load_address;
    header->build_id = build_id;
    header->header_crc32 = firmware_image_header_crc(header);

    return FIRMWARE_IMAGE_OK;
}

firmware_image_result_t firmware_image_header_validate(const firmware_image_header_t *header)
{
    if (header == NULL) {
        return FIRMWARE_IMAGE_ERR_INVALID_ARG;
    }

    /* Checked before anything else, so erased flash (all 0xFF) and blank flash
       (all 0x00) both fail here rather than deeper in. */
    if (header->magic != FIRMWARE_IMAGE_MAGIC) {
        return FIRMWARE_IMAGE_ERR_BAD_MAGIC;
    }
    if (header->header_version != FIRMWARE_IMAGE_HEADER_VERSION ||
        header->header_size != sizeof(firmware_image_header_t)) {
        return FIRMWARE_IMAGE_ERR_BAD_VERSION;
    }
    if (header->payload_size == 0 || header->payload_size > FIRMWARE_IMAGE_MAX_PAYLOAD) {
        return FIRMWARE_IMAGE_ERR_BAD_SIZE;
    }
    if (header->header_crc32 != firmware_image_header_crc(header)) {
        return FIRMWARE_IMAGE_ERR_BAD_HEADER_CRC;
    }

    return FIRMWARE_IMAGE_OK;
}

firmware_image_result_t firmware_image_verify_payload(const firmware_image_header_t *header,
                                                      uint32_t payload_crc32)
{
    const firmware_image_result_t header_result = firmware_image_header_validate(header);
    if (header_result != FIRMWARE_IMAGE_OK) {
        return header_result;
    }

    return (header->payload_crc32 == payload_crc32)
        ? FIRMWARE_IMAGE_OK
        : FIRMWARE_IMAGE_ERR_BAD_PAYLOAD_CRC;
}

const char *firmware_image_result_name(firmware_image_result_t result)
{
    switch (result) {
        case FIRMWARE_IMAGE_OK:                  return "ok";
        case FIRMWARE_IMAGE_ERR_INVALID_ARG:     return "invalid argument";
        case FIRMWARE_IMAGE_ERR_BAD_MAGIC:       return "not a firmware header";
        case FIRMWARE_IMAGE_ERR_BAD_VERSION:     return "unsupported header version";
        case FIRMWARE_IMAGE_ERR_BAD_SIZE:        return "implausible payload size";
        case FIRMWARE_IMAGE_ERR_BAD_HEADER_CRC:  return "damaged header";
        case FIRMWARE_IMAGE_ERR_BAD_PAYLOAD_CRC: return "damaged payload";
        default:                                 return "unknown";
    }
}
