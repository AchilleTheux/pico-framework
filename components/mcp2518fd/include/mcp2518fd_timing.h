#ifndef PICO_FRAMEWORK_MCP2518FD_TIMING_H
#define PICO_FRAMEWORK_MCP2518FD_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t nbtcfg;
    uint32_t dbtcfg;
    uint32_t tdc;
} mcp2518fd_bit_timing_t;

/* SYSCLK is 20 or 40 MHz (4 MHz crystals require the PLL). Exact rates
   only; nominal <=1 Mbit/s, data <=8 Mbit/s and >= nominal. A zero data
   rate selects classic CAN. Prefer an 80% sample point, then the smallest
   prescaler. TDC uses an automatic measured delay above 1 Mbit/s. */
bool mcp2518fd_compute_bit_timing(uint32_t sysclk_hz, uint32_t nominal_bitrate,
                                uint32_t data_bitrate, mcp2518fd_bit_timing_t *timing);

#ifdef __cplusplus
}
#endif
#endif
