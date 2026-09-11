#include <stddef.h>
#include "mcp2518fd_timing.h"

static bool phase(uint32_t clock, uint32_t bitrate, bool data, uint32_t *reg, uint32_t *offset)
{
    if (bitrate == 0 || clock % bitrate != 0) return false;
    uint32_t target = clock / bitrate;
    uint32_t best_error = UINT32_MAX;
    bool found = false;
    for (uint32_t brp = 1; brp <= 256; brp++) {
        if (target % brp != 0) continue;
        uint32_t tq = target / brp;
        if (tq < 4 || tq > (data ? 49u : 385u)) continue;
        for (uint32_t seg2 = 1; seg2 <= (data ? 16u : 128u) && seg2 + 1 < tq; seg2++) {
            uint32_t seg1 = tq - seg2 - 1;
            if (seg1 > (data ? 32u : 256u)) continue;
            uint32_t sample = (tq - seg2) * 10000u / tq;
            uint32_t error = sample > 8000 ? sample - 8000 : 8000 - sample;
            if (found && error >= best_error) continue;
            *reg = ((brp - 1) << 24) | ((seg1 - 1) << 16) |
                   ((seg2 - 1) << 8) | (seg2 - 1);
            *offset = brp * seg1;
            best_error = error;
            found = true;
        }
    }
    return found;
}

bool mcp2518fd_compute_bit_timing(uint32_t sysclk_hz, uint32_t nominal_bitrate,
                                uint32_t data_bitrate, mcp2518fd_bit_timing_t *timing)
{
    if (timing == NULL || (sysclk_hz != 20000000u && sysclk_hz != 40000000u) ||
        nominal_bitrate == 0 || nominal_bitrate > 1000000u || data_bitrate > 8000000u ||
        (data_bitrate != 0 && data_bitrate < nominal_bitrate)) return false;
    mcp2518fd_bit_timing_t result = {0};
    uint32_t offset;
    if (!phase(sysclk_hz, nominal_bitrate, false, &result.nbtcfg, &offset)) return false;
    if (data_bitrate != 0) {
        if (!phase(sysclk_hz, data_bitrate, true, &result.dbtcfg, &offset)) return false;
        if (data_bitrate > 1000000u) {
            if (offset > 63) return false;
            result.tdc = (2u << 16) | (offset << 8);
        }
    }
    *timing = result;
    return true;
}
