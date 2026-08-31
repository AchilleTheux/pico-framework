/*
 * vl53l0x_timing - the sensor's timing arithmetic.
 *
 * Separated from the driver, and free of the Pico SDK, because this is where
 * VL53L0X drivers actually go wrong. The sensor's measurement budget is spent
 * across several ranging phases, each with its own overhead and its own
 * timeout in a compressed format, and the conversions between microseconds,
 * macro clocks and that format are unobvious integer maths with no feedback
 * when they are wrong — a mis-set budget shows up as a sensor that reads short,
 * or times out, or drifts with temperature, none of which points at the sum.
 *
 * All of it is unit-tested on the host.
 *
 * The formulas are ST's, by way of the widely used Pololu implementation. They
 * are reproduced rather than reinvented: the constants below are measured
 * properties of the part, not choices.
 */

#ifndef PICO_FRAMEWORK_VL53L0X_TIMING_H
#define PICO_FRAMEWORK_VL53L0X_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed overheads of each ranging phase, in microseconds, and the floor below
 * which a budget cannot be satisfied. From ST's API.
 */
#define VL53L0X_START_OVERHEAD_US 1910u
#define VL53L0X_END_OVERHEAD_US 960u
#define VL53L0X_MSRC_OVERHEAD_US 660u
#define VL53L0X_TCC_OVERHEAD_US 590u
#define VL53L0X_DSS_OVERHEAD_US 690u
#define VL53L0X_PRE_RANGE_OVERHEAD_US 660u
#define VL53L0X_FINAL_RANGE_OVERHEAD_US 550u

#define VL53L0X_MIN_TIMING_BUDGET_US 20000u

/* Which phases the sequence-config register has enabled. */
typedef struct {
    bool tcc;         /* target centre check */
    bool dss;         /* dynamic SPAD selection */
    bool msrc;        /* minimum signal rate check */
    bool pre_range;
    bool final_range;
} vl53l0x_sequence_steps_t;

/* Timeouts read back from the sensor, in macro clocks and microseconds. */
typedef struct {
    uint8_t pre_range_vcsel_period_pclks;
    uint8_t final_range_vcsel_period_pclks;

    uint16_t msrc_dss_tcc_mclks;
    uint16_t pre_range_mclks;
    uint16_t final_range_mclks;

    uint32_t msrc_dss_tcc_us;
    uint32_t pre_range_us;
    uint32_t final_range_us;
} vl53l0x_sequence_timeouts_t;

/* ---------------------------------------------------------------------------
 * VCSEL period
 *
 * Stored in the register as (period / 2) - 1, so only even periods exist.
 * -------------------------------------------------------------------------*/

static inline uint8_t vl53l0x_decode_vcsel_period(uint8_t register_value)
{
    return (uint8_t)((register_value + 1u) << 1);
}

static inline uint8_t vl53l0x_encode_vcsel_period(uint8_t period_pclks)
{
    return (uint8_t)((period_pclks >> 1) - 1u);
}

/*
 * Length of one macro period in nanoseconds, for a given VCSEL period.
 *
 * 2304 PLL periods of 1655 ps each. The +500 before dividing rounds to the
 * nearest nanosecond rather than truncating, which matters because this value
 * multiplies every timeout.
 */
static inline uint32_t vl53l0x_macro_period_ns(uint8_t vcsel_period_pclks)
{
    return (uint32_t)(((2304u * (uint32_t)vcsel_period_pclks * 1655u) + 500u) / 1000u);
}

/* ---------------------------------------------------------------------------
 * Timeouts
 * -------------------------------------------------------------------------*/

uint32_t vl53l0x_timeout_mclks_to_us(uint16_t timeout_mclks, uint8_t vcsel_period_pclks);
uint32_t vl53l0x_timeout_us_to_mclks(uint32_t timeout_us, uint8_t vcsel_period_pclks);

/*
 * The sensor stores a timeout as (mantissa << exponent) + 1, in one 16-bit
 * register: the low byte is the mantissa and the high byte the exponent.
 *
 * The encoding keeps eight bits of mantissa, so it is lossy for anything above
 * 256 — and lossy *downward*. A decoded value is therefore never longer than
 * what was asked for, which is the safe direction: a timeout that came back
 * slightly shorter ends a measurement early, where one that came back longer
 * would overrun the budget the caller set.
 */
uint16_t vl53l0x_encode_timeout(uint32_t timeout_mclks);
uint32_t vl53l0x_decode_timeout(uint16_t register_value);

/* ---------------------------------------------------------------------------
 * The budget
 * -------------------------------------------------------------------------*/

/*
 * Total microseconds the enabled phases will take, given their timeouts.
 * This is what a caller's budget has to cover.
 */
uint32_t vl53l0x_budget_used_us(const vl53l0x_sequence_steps_t *steps,
                                const vl53l0x_sequence_timeouts_t *timeouts);

/*
 * How long the final range phase may take, for a total budget of `budget_us`.
 *
 * Returns false when the budget cannot be met — the fixed overheads and the
 * other phases already exceed it — rather than silently clamping, because a
 * budget that was quietly ignored is indistinguishable from one that was
 * honoured until the measurements come back wrong.
 */
bool vl53l0x_final_range_timeout_us(const vl53l0x_sequence_steps_t *steps,
                                    const vl53l0x_sequence_timeouts_t *timeouts,
                                    uint32_t budget_us,
                                    uint32_t *final_range_us);

/* Decode the sequence-config register into the phases it enables. */
vl53l0x_sequence_steps_t vl53l0x_decode_sequence_steps(uint8_t register_value);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_VL53L0X_TIMING_H */
