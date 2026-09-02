#include <stddef.h>

#include "mcp2515_timing.h"

/*
 * The MCP2515 divides its oscillator by 2*(BRP+1) to get one Time Quantum,
 * and a bit is some whole number of Time Quanta (NTQ) long:
 *
 *   bit_period = NTQ * 2 * (BRP+1) / oscillator_hz
 *
 * so an exact bit rate needs
 *
 *   NTQ * (BRP+1) = oscillator_hz / (2 * bitrate)
 *
 * to be an exact integer, with NTQ in the controller's valid 8..25 range and
 * BRP in its 6-bit 0..63 range. Several (NTQ, BRP) pairs can satisfy that;
 * this picks the one closest to 16 quanta, a conventional middle ground
 * between timing resolution and reach (more quanta needs a higher BRP,
 * i.e. a slower quantum, which narrows how many oscillators can hit an
 * exact rate at all).
 */
#define MIN_TQ_PER_BIT 8u
#define MAX_TQ_PER_BIT 25u
#define MAX_BRP 63u
#define PREFERRED_TQ_PER_BIT 16u

#define MAX_PROP_PHSEG1_SUM 16u /* PRSEG (1-8) + PHSEG1 (1-8) */
#define MIN_PHSEG2 2u
#define MAX_PHSEG2 8u

static bool find_ntq_and_brp(uint32_t oscillator_hz, uint32_t bitrate,
                              uint32_t *out_ntq, uint32_t *out_brp)
{
    const uint64_t denominator = 2ull * (uint64_t)bitrate;
    if (denominator == 0 || (uint64_t)oscillator_hz % denominator != 0) {
        return false;
    }
    const uint64_t target = (uint64_t)oscillator_hz / denominator; /* NTQ*(BRP+1) */

    bool found = false;
    uint32_t best_ntq = 0;
    uint32_t best_brp = 0;
    uint32_t best_distance = 0;

    for (uint32_t ntq = MIN_TQ_PER_BIT; ntq <= MAX_TQ_PER_BIT; ntq++) {
        if (target % ntq != 0) {
            continue;
        }
        const uint64_t brp_plus_one = target / ntq;
        if (brp_plus_one == 0 || brp_plus_one > (uint64_t)(MAX_BRP + 1u)) {
            continue;
        }
        const uint32_t distance = ntq > PREFERRED_TQ_PER_BIT
                                       ? ntq - PREFERRED_TQ_PER_BIT
                                       : PREFERRED_TQ_PER_BIT - ntq;
        if (!found || distance < best_distance) {
            found = true;
            best_ntq = ntq;
            best_brp = (uint32_t)(brp_plus_one - 1u);
            best_distance = distance;
        }
    }

    if (found) {
        *out_ntq = best_ntq;
        *out_brp = best_brp;
    }
    return found;
}

/*
 * Split the quanta after the fixed 1-quantum sync segment between
 * PRSEG+PHSEG1 (before the sample point) and PHSEG2 (after it), biased
 * toward roughly a 3/4 sample point. The exact split between PRSEG and
 * PHSEG1 has no electrical effect — both only ever appear as a sum in the
 * controller's actual bit timing — so it is simply divided evenly.
 */
static void split_segments(uint32_t ntq, uint32_t *out_prseg, uint32_t *out_phseg1,
                            uint32_t *out_phseg2)
{
    const uint32_t remaining = ntq - 1u; /* 7..24 */

    uint32_t phseg2 = remaining / 4u;
    if (phseg2 < MIN_PHSEG2) {
        phseg2 = MIN_PHSEG2;
    }
    if (phseg2 > MAX_PHSEG2) {
        phseg2 = MAX_PHSEG2;
    }

    uint32_t ps_sum = remaining - phseg2;
    if (ps_sum > MAX_PROP_PHSEG1_SUM) {
        ps_sum = MAX_PROP_PHSEG1_SUM;
        phseg2 = remaining - ps_sum;
    }

    uint32_t prseg = ps_sum / 2u;
    if (prseg < 1u) {
        prseg = 1u;
    }
    uint32_t phseg1 = ps_sum - prseg;
    if (phseg1 < 1u) {
        phseg1 = 1u;
        prseg = ps_sum - phseg1;
    }

    *out_prseg = prseg;
    *out_phseg1 = phseg1;
    *out_phseg2 = phseg2;
}

bool mcp2515_compute_bit_timing(uint32_t oscillator_hz, uint32_t bitrate,
                                 mcp2515_bit_timing_t *timing)
{
    if (timing == NULL || oscillator_hz == 0 || bitrate == 0) {
        return false;
    }

    uint32_t ntq;
    uint32_t brp;
    if (!find_ntq_and_brp(oscillator_hz, bitrate, &ntq, &brp)) {
        return false;
    }

    uint32_t prseg;
    uint32_t phseg1;
    uint32_t phseg2;
    split_segments(ntq, &prseg, &phseg1, &phseg2);

    const uint32_t sjw = 1u; /* quanta; a conservative, always-valid default */

    timing->cnf1 = (uint8_t)(((sjw - 1u) << 6) | brp);
    timing->cnf2 = (uint8_t)(0x80u /* BTLMODE: PHSEG2 set explicitly by CNF3 */
                              | ((phseg1 - 1u) << 3) | (prseg - 1u));
    timing->cnf3 = (uint8_t)(phseg2 - 1u);
    return true;
}

/* Datasheet section 8.1. */
#define RESET_OSCILLATOR_CYCLES 128u

/*
 * Neither bound is a controller limit — the fastest part in the family takes
 * 40 MHz — they only keep a nonsense argument out of the arithmetic below.
 * Clamping low gives the longest wait rather than none; clamping high keeps
 * the rounding-up numerator inside 32 bits.
 */
#define RESET_MIN_OSCILLATOR_HZ 1000000u
#define RESET_MAX_OSCILLATOR_HZ 100000000u

uint32_t mcp2515_reset_delay_us(uint32_t oscillator_hz)
{
    uint32_t hz = oscillator_hz;
    if (hz < RESET_MIN_OSCILLATOR_HZ) {
        hz = RESET_MIN_OSCILLATOR_HZ;
    } else if (hz > RESET_MAX_OSCILLATOR_HZ) {
        hz = RESET_MAX_OSCILLATOR_HZ;
    }

    /* Cycles to microseconds, rounded up: (128 * 1e6 + hz - 1) / hz. With hz
       bounded above, the numerator is at most 2.28e8 and stays in 32 bits. */
    const uint32_t exact = (RESET_OSCILLATOR_CYCLES * 1000000u + hz - 1u) / hz;

    /* Doubled: a few microseconds once at startup, against a resonator that
       starts slowly being reported as no controller at all. */
    return exact * 2u;
}
