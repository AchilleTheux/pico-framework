#include <stddef.h>

#include "vl53l0x_timing.h"

uint32_t vl53l0x_timeout_mclks_to_us(uint16_t timeout_mclks, uint8_t vcsel_period_pclks)
{
    const uint32_t macro_ns = vl53l0x_macro_period_ns(vcsel_period_pclks);
    return (((uint32_t)timeout_mclks * macro_ns) + 500u) / 1000u;
}

uint32_t vl53l0x_timeout_us_to_mclks(uint32_t timeout_us, uint8_t vcsel_period_pclks)
{
    const uint32_t macro_ns = vl53l0x_macro_period_ns(vcsel_period_pclks);
    if (macro_ns == 0) {
        return 0;
    }
    return (((timeout_us * 1000u) + (macro_ns / 2u)) / macro_ns);
}

uint16_t vl53l0x_encode_timeout(uint32_t timeout_mclks)
{
    if (timeout_mclks == 0) {
        return 0;
    }

    uint32_t mantissa = timeout_mclks - 1u;
    uint8_t exponent = 0;

    /* Shift until the mantissa fits in a byte, counting the shifts. This is
       where the precision goes for large timeouts. */
    while ((mantissa & 0xFFFFFF00u) != 0) {
        mantissa >>= 1;
        exponent++;
    }

    return (uint16_t)(((uint16_t)exponent << 8) | (mantissa & 0xFFu));
}

uint32_t vl53l0x_decode_timeout(uint16_t register_value)
{
    const uint32_t mantissa = register_value & 0x00FFu;
    const uint32_t exponent = (register_value & 0xFF00u) >> 8;

    return (mantissa << exponent) + 1u;
}

vl53l0x_sequence_steps_t vl53l0x_decode_sequence_steps(uint8_t register_value)
{
    /* Bit positions from ST's sequence config register. */
    return (vl53l0x_sequence_steps_t){
        .tcc         = (register_value & 0x10u) != 0,
        .dss         = (register_value & 0x08u) != 0,
        .msrc        = (register_value & 0x04u) != 0,
        .pre_range   = (register_value & 0x40u) != 0,
        .final_range = (register_value & 0x80u) != 0,
    };
}

uint32_t vl53l0x_budget_used_us(const vl53l0x_sequence_steps_t *steps,
                                const vl53l0x_sequence_timeouts_t *timeouts)
{
    if (steps == NULL || timeouts == NULL) {
        return 0;
    }

    uint32_t used = VL53L0X_START_OVERHEAD_US + VL53L0X_END_OVERHEAD_US;

    if (steps->tcc) {
        used += timeouts->msrc_dss_tcc_us + VL53L0X_TCC_OVERHEAD_US;
    }

    /*
     * DSS runs the msrc/dss/tcc timeout twice, and takes the place of MSRC
     * rather than adding to it — they share the same measurement. Adding both
     * would over-count and make every budget look unaffordable.
     */
    if (steps->dss) {
        used += 2u * (timeouts->msrc_dss_tcc_us + VL53L0X_DSS_OVERHEAD_US);
    } else if (steps->msrc) {
        used += timeouts->msrc_dss_tcc_us + VL53L0X_MSRC_OVERHEAD_US;
    }

    if (steps->pre_range) {
        used += timeouts->pre_range_us + VL53L0X_PRE_RANGE_OVERHEAD_US;
    }
    if (steps->final_range) {
        used += timeouts->final_range_us + VL53L0X_FINAL_RANGE_OVERHEAD_US;
    }

    return used;
}

bool vl53l0x_final_range_timeout_us(const vl53l0x_sequence_steps_t *steps,
                                    const vl53l0x_sequence_timeouts_t *timeouts,
                                    uint32_t budget_us,
                                    uint32_t *final_range_us)
{
    if (steps == NULL || timeouts == NULL || final_range_us == NULL) {
        return false;
    }
    if (budget_us < VL53L0X_MIN_TIMING_BUDGET_US) {
        return false;
    }
    if (!steps->final_range) {
        /* Nothing to allocate: without the final range phase there is no
           measurement to budget for. */
        *final_range_us = 0;
        return true;
    }

    /*
     * Everything except the final range phase's own timeout, which is what is
     * being solved for — so the final range timeout is excluded here and its
     * fixed overhead included.
     */
    const vl53l0x_sequence_timeouts_t without_final = {
        .msrc_dss_tcc_us = timeouts->msrc_dss_tcc_us,
        .pre_range_us = timeouts->pre_range_us,
        .final_range_us = 0,
    };
    vl53l0x_sequence_steps_t others = *steps;
    others.final_range = false;

    const uint32_t used = vl53l0x_budget_used_us(&others, &without_final) +
                          VL53L0X_FINAL_RANGE_OVERHEAD_US;

    if (used >= budget_us) {
        return false;
    }

    *final_range_us = budget_us - used;
    return true;
}
