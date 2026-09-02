/*
 * mcp2515_timing - CNF1/CNF2/CNF3 bit-timing register values for an MCP2515
 * (and pin/register-compatible clones such as the XL2515) at an arbitrary
 * oscillator frequency and bit rate.
 *
 * SDK-independent and arithmetic only, so it is host-tested rather than
 * trusted from a transcribed vendor table: a mis-copied digit in a table is
 * exactly the kind of mistake that only shows up as an unreadable bus.
 */

#ifndef PICO_FRAMEWORK_MCP2515_TIMING_H
#define PICO_FRAMEWORK_MCP2515_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t cnf1;
    uint8_t cnf2;
    uint8_t cnf3;
} mcp2515_bit_timing_t;

/*
 * Compute CNF1/2/3 for `bitrate` bit/s from an `oscillator_hz` crystal or
 * resonator.
 *
 * Returns false when no register combination reproduces the bit rate
 * *exactly* — a mistimed bus fails quietly rather than refusing to build, so
 * this never returns an approximation. A false result means the oscillator
 * and bit rate are not an exact multiple pair for this controller; pick a
 * bit rate that is (bitrates that divide the oscillator by a power of two
 * times a small integer are the ones drivers publish tables for, and this
 * function finds every one of them, not just those).
 */
bool mcp2515_compute_bit_timing(uint32_t oscillator_hz, uint32_t bitrate,
                                 mcp2515_bit_timing_t *timing);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MCP2515_TIMING_H */
