#include "crc.h"

/*
 * Bitwise rather than table-driven, deliberately.
 *
 * A 256-entry CRC-32 table costs 1 KiB of flash to save about seven cycles a
 * byte. The two callers here are a firmware update and a log write, neither of
 * which is in a hot path: checksumming a 64 KiB image bitwise takes a few
 * milliseconds on an RP2040, against a transfer that takes seconds.
 *
 * Both RP2040 and RP2350 can also compute CRC-32 in hardware through the DMA
 * sniffer, which is far faster still. It is not used here because it would
 * make the component claim a DMA channel and stop being host-testable, which
 * is a poor trade for the speed this actually needs.
 */

uint32_t crc32_update(uint32_t state, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (bytes == NULL) {
        return state;
    }

    for (size_t i = 0; i < len; i++) {
        state ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            /* Reflected form, so the register shifts right. */
            state = (state & 1u) ? ((state >> 1) ^ 0xEDB88320u) : (state >> 1);
        }
    }
    return state;
}

uint32_t crc32(const void *data, size_t len)
{
    return crc32_end(crc32_update(crc32_begin(), data, len));
}

uint16_t crc16_ccitt_update(uint16_t state, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (bytes == NULL) {
        return state;
    }

    for (size_t i = 0; i < len; i++) {
        /* Non-reflected form: the byte enters at the top and shifts left. */
        state ^= (uint16_t)((uint16_t)bytes[i] << 8);
        for (unsigned bit = 0; bit < 8; bit++) {
            state = (state & 0x8000u) ? (uint16_t)((state << 1) ^ 0x1021u)
                                      : (uint16_t)(state << 1);
        }
    }
    return state;
}

uint16_t crc16_ccitt(const void *data, size_t len)
{
    return crc16_ccitt_end(crc16_ccitt_update(crc16_ccitt_begin(), data, len));
}
