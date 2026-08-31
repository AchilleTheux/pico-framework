#include <string.h>

#include "crc.h"

#include "persistent_config.h"

/*
 * Written at the start of a slot, before the records.
 *
 * The sequence number is what makes the two slots work: whichever is higher is
 * current. The CRC covers the records, and the header has its own, so a
 * half-written slot is recognisable before its contents are read.
 */
typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t length;       /* bytes of records that follow */
    uint32_t records_crc32;
    uint32_t header_crc32;
} slot_header_t;

_Static_assert(sizeof(slot_header_t) == 20,
               "the slot header goes to flash and must not change size");

/* Flash programs whole pages, so the records start on the next page boundary
   after the header rather than immediately after it. */
#define RECORDS_AT FLASH_LAYOUT_PAGE_SIZE

#define HEADER_CRC_COVERAGE (sizeof(slot_header_t) - sizeof(uint32_t))

size_t persistent_config_capacity(void)
{
    return FLASH_LAYOUT_SECTOR_SIZE - RECORDS_AT;
}

static uint32_t header_crc(const slot_header_t *header)
{
    return crc32(header, HEADER_CRC_COVERAGE);
}

static bool slot_regions(persistent_config_t *config)
{
    const flash_layout_t *layout = flash_layout_get();

    /* Two sectors at the front of the data region; logs come after. */
    if (layout->data.size < 2u * FLASH_LAYOUT_SECTOR_SIZE) {
        return false;
    }

    config->slot[0].offset = layout->data.offset;
    config->slot[0].size = FLASH_LAYOUT_SECTOR_SIZE;
    config->slot[1].offset = layout->data.offset + FLASH_LAYOUT_SECTOR_SIZE;
    config->slot[1].size = FLASH_LAYOUT_SECTOR_SIZE;
    return true;
}

/* Read and check one slot. */
static bool slot_read(const flash_region_t *slot, slot_header_t *header,
                      const uint8_t **records)
{
    const uint8_t *raw = flash_storage_data(slot, 0, sizeof(slot_header_t));
    if (raw == NULL) {
        return false;
    }
    memcpy(header, raw, sizeof(*header));

    if (header->magic != PERSISTENT_CONFIG_MAGIC) {
        return false;
    }
    if (header->header_crc32 != header_crc(header)) {
        return false;
    }
    if (header->length > persistent_config_capacity()) {
        return false;
    }

    const uint8_t *payload = flash_storage_data(slot, RECORDS_AT, header->length);
    if (payload == NULL) {
        return false;
    }
    if (crc32(payload, header->length) != header->records_crc32) {
        return false;
    }

    *records = payload;
    return true;
}

/*
 * Which of two sequence numbers is later.
 *
 * Compared as a signed difference rather than directly, so the answer stays
 * right when the counter wraps. It would take 4 billion saves to get there, but
 * the alternative is a comparison that is wrong exactly once and silently
 * reverts the configuration to an ancient one.
 */
static bool sequence_is_later(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

persistent_config_result_t persistent_config_load(persistent_config_t *config,
                                                  uint8_t *buffer, size_t capacity)
{
    if (config == NULL || buffer == NULL || capacity == 0) {
        return PERSISTENT_CONFIG_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));
    if (!slot_regions(config)) {
        config_store_init(&config->store, buffer, capacity);
        return PERSISTENT_CONFIG_ERR_NO_SPACE;
    }

    slot_header_t header[2];
    const uint8_t *records[2] = { NULL, NULL };
    const bool valid[2] = {
        slot_read(&config->slot[0], &header[0], &records[0]),
        slot_read(&config->slot[1], &header[1], &records[1]),
    };

    int chosen = -1;
    if (valid[0] && valid[1]) {
        chosen = sequence_is_later(header[1].sequence, header[0].sequence) ? 1 : 0;
    } else if (valid[0]) {
        chosen = 0;
    } else if (valid[1]) {
        chosen = 1;
    }

    if (chosen < 0) {
        config_store_init(&config->store, buffer, capacity);
        config->current = 0;
        config->sequence = 0;

        /*
         * Both slots unreadable. Distinguishing "never saved" from "damaged"
         * matters: the first is normal on a new board and the second means
         * something went wrong, and a caller that logs one and not the other
         * needs to be able to tell.
         */
        const bool blank = flash_storage_is_erased(&config->slot[0], 0,
                                                   sizeof(slot_header_t)) &&
                           flash_storage_is_erased(&config->slot[1], 0,
                                                   sizeof(slot_header_t));
        return blank ? PERSISTENT_CONFIG_ERR_EMPTY : PERSISTENT_CONFIG_ERR_CORRUPT;
    }

    if (header[chosen].length > capacity) {
        config_store_init(&config->store, buffer, capacity);
        return PERSISTENT_CONFIG_ERR_TOO_LARGE;
    }

    memcpy(buffer, records[chosen], header[chosen].length);

    const config_result_t adopted =
        config_store_load(&config->store, buffer, capacity, header[chosen].length);
    if (adopted != CONFIG_OK) {
        config_store_init(&config->store, buffer, capacity);
        return PERSISTENT_CONFIG_ERR_CORRUPT;
    }

    config->current = (uint8_t)chosen;
    config->sequence = header[chosen].sequence;
    config->loaded = true;
    return PERSISTENT_CONFIG_OK;
}

persistent_config_result_t persistent_config_save(persistent_config_t *config)
{
    if (config == NULL || config->store.buffer == NULL) {
        return PERSISTENT_CONFIG_ERR_INVALID_ARG;
    }
    if (config->slot[0].size == 0) {
        return PERSISTENT_CONFIG_ERR_NO_SPACE;
    }

    const size_t length = config_store_used(&config->store);
    if (length > persistent_config_capacity()) {
        return PERSISTENT_CONFIG_ERR_TOO_LARGE;
    }

    /* The slot that is not current, so the current one survives a failure
       anywhere in what follows. */
    const uint8_t target = config->loaded ? (uint8_t)(1u - config->current) : 0u;
    const flash_region_t *slot = &config->slot[target];

    if (flash_storage_erase(slot, 0, slot->size) != FLASH_STORAGE_OK) {
        return PERSISTENT_CONFIG_ERR_FLASH;
    }

    /*
     * Records first, header last. Until the header lands the slot has no valid
     * magic, so an interruption here leaves it simply unreadable rather than
     * readable and wrong — and the other slot is still current either way.
     */
    if (length > 0) {
        uint8_t page[FLASH_LAYOUT_PAGE_SIZE];
        size_t written = 0;
        while (written < length) {
            const size_t chunk = (length - written < sizeof(page)) ? length - written
                                                                   : sizeof(page);
            /* Padded with the erased value so the tail of the last page stays
               writable. */
            memset(page, 0xFF, sizeof(page));
            memcpy(page, &config->store.buffer[written], chunk);

            if (flash_storage_program_verified(slot, RECORDS_AT + written, page,
                                               sizeof(page)) != FLASH_STORAGE_OK) {
                return PERSISTENT_CONFIG_ERR_FLASH;
            }
            written += chunk;
        }
    }

    slot_header_t header = {
        .magic = PERSISTENT_CONFIG_MAGIC,
        .sequence = config->sequence + 1u,
        .length = (uint32_t)length,
        .records_crc32 = crc32(config->store.buffer, length),
        .header_crc32 = 0,
    };
    header.header_crc32 = header_crc(&header);

    uint8_t page[FLASH_LAYOUT_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &header, sizeof(header));

    if (flash_storage_program_verified(slot, 0, page, sizeof(page)) != FLASH_STORAGE_OK) {
        return PERSISTENT_CONFIG_ERR_FLASH;
    }

    config->current = target;
    config->sequence = header.sequence;
    config->loaded = true;
    return PERSISTENT_CONFIG_OK;
}

persistent_config_result_t persistent_config_erase(persistent_config_t *config)
{
    if (config == NULL || config->slot[0].size == 0) {
        return PERSISTENT_CONFIG_ERR_INVALID_ARG;
    }

    for (unsigned i = 0; i < 2; i++) {
        if (flash_storage_erase(&config->slot[i], 0, config->slot[i].size)
                != FLASH_STORAGE_OK) {
            return PERSISTENT_CONFIG_ERR_FLASH;
        }
    }

    config_clear(&config->store);
    config->loaded = false;
    config->sequence = 0;
    config->current = 0;
    return PERSISTENT_CONFIG_OK;
}

const char *persistent_config_result_name(persistent_config_result_t result)
{
    switch (result) {
        case PERSISTENT_CONFIG_OK:                return "ok";
        case PERSISTENT_CONFIG_ERR_INVALID_ARG:   return "invalid argument";
        case PERSISTENT_CONFIG_ERR_NO_SPACE:      return "no room for two slots";
        case PERSISTENT_CONFIG_ERR_EMPTY:         return "nothing saved yet";
        case PERSISTENT_CONFIG_ERR_CORRUPT:       return "saved data is damaged";
        case PERSISTENT_CONFIG_ERR_TOO_LARGE:     return "too large for a slot";
        case PERSISTENT_CONFIG_ERR_FLASH:         return "flash write failed";
        default:                                  return "unknown";
    }
}
