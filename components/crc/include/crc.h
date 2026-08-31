/*
 * crc - cyclic redundancy checks.
 *
 * Two algorithms, both in their standard parameterisations so a value computed
 * here matches one computed by any other tool:
 *
 *   crc32       IEEE 802.3, reflected, polynomial 0xEDB88320, init and final
 *               xor 0xFFFFFFFF. What zip, PNG and Ethernet use, and what
 *               `crc32` on the command line prints.
 *   crc16_ccitt CRC-16/CCITT-FALSE, polynomial 0x1021, init 0xFFFF, no final
 *               xor. Common in serial framing.
 *
 * Both come in a one-shot form and an incremental form. The incremental form
 * is the one that matters here: a firmware image is checksummed as it arrives
 * in chunks and again as it is read back from flash, and neither fits in RAM.
 *
 * No Pico SDK dependency, so this is unit-tested on the host against the
 * published check values.
 */

#ifndef PICO_FRAMEWORK_CRC_H
#define PICO_FRAMEWORK_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * CRC-32
 *
 *   uint32_t state = crc32_begin();
 *   state = crc32_update(state, chunk, len);   ... repeated per chunk
 *   uint32_t result = crc32_end(state);
 *
 * The running state is *not* the CRC: it is the raw register, still inverted.
 * Only crc32_end() produces a comparable value. Storing a running state and
 * calling it a checksum is the classic way to get two tools disagreeing.
 * -------------------------------------------------------------------------*/

#define CRC32_INITIAL 0xFFFFFFFFu

static inline uint32_t crc32_begin(void)
{
    return CRC32_INITIAL;
}

uint32_t crc32_update(uint32_t state, const void *data, size_t len);

static inline uint32_t crc32_end(uint32_t state)
{
    return state ^ 0xFFFFFFFFu;
}

/* Equivalent to begin/update/end over one buffer. */
uint32_t crc32(const void *data, size_t len);

/* ---------------------------------------------------------------------------
 * CRC-16/CCITT-FALSE
 * -------------------------------------------------------------------------*/

#define CRC16_CCITT_INITIAL 0xFFFFu

static inline uint16_t crc16_ccitt_begin(void)
{
    return CRC16_CCITT_INITIAL;
}

uint16_t crc16_ccitt_update(uint16_t state, const void *data, size_t len);

/* No final xor in this parameterisation; present for symmetry with crc32. */
static inline uint16_t crc16_ccitt_end(uint16_t state)
{
    return state;
}

uint16_t crc16_ccitt(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CRC_H */
