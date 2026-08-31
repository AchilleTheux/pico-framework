/*
 * Host-side tests for the VL53L0X timing arithmetic.
 *
 * This is where drivers for this part go wrong. The measurement budget is
 * shared across ranging phases with fixed overheads, and each phase's timeout
 * is stored in a compressed format that loses precision. Nothing gives feedback
 * when the sum is wrong: the sensor just reads short, or times out, or drifts,
 * and none of that points at the arithmetic.
 *
 * The expected values below were derived from ST's formulas independently, not
 * captured from this code.
 */

#include "test.h"

#include "vl53l0x_timing.h"

/* The two VCSEL periods the default configuration uses. */
#define PRE_RANGE_VCSEL 14u
#define FINAL_RANGE_VCSEL 14u

/* ---------------------------------------------------------------------------
 * VCSEL period
 * -------------------------------------------------------------------------*/

TEST(the_vcsel_period_register_holds_half_the_period_less_one)
{
    CHECK_EQ_INT(vl53l0x_decode_vcsel_period(6), 14);
    CHECK_EQ_INT(vl53l0x_encode_vcsel_period(14), 6);
    CHECK_EQ_INT(vl53l0x_decode_vcsel_period(8), 18);
    CHECK_EQ_INT(vl53l0x_encode_vcsel_period(18), 8);
}

TEST(every_even_vcsel_period_round_trips)
{
    /* Only even periods are representable, which is why the API takes the
       period rather than the register value — a caller asking for 13 would
       otherwise get 12 or 14 without being told. */
    for (uint8_t period = 4; period <= 18; period += 2) {
        const uint8_t encoded = vl53l0x_encode_vcsel_period(period);
        if (vl53l0x_decode_vcsel_period(encoded) != period) {
            printf("    period %u did not round-trip\n", period);
            CHECK(false);
            return;
        }
    }
}

TEST(the_macro_period_matches_the_datasheet_formula)
{
    /* 2304 PLL periods of 1655 ps, rounded to the nearest nanosecond. */
    CHECK_EQ_U32(vl53l0x_macro_period_ns(14), 53384u);
    CHECK_EQ_U32(vl53l0x_macro_period_ns(10), 38131u);
    CHECK_EQ_U32(vl53l0x_macro_period_ns(12), 45757u);
    CHECK_EQ_U32(vl53l0x_macro_period_ns(18), 68636u);
    CHECK_EQ_U32(vl53l0x_macro_period_ns(8), 30505u);
}

TEST(a_longer_vcsel_period_gives_a_longer_macro_period)
{
    /* Monotonic, which is what makes long-range mode work: a longer VCSEL
       period buys more time per macro clock. */
    uint32_t previous = 0;
    for (uint8_t period = 4; period <= 18; period += 2) {
        const uint32_t macro = vl53l0x_macro_period_ns(period);
        if (macro <= previous) {
            CHECK(false);
            return;
        }
        previous = macro;
    }
}

/* ---------------------------------------------------------------------------
 * The compressed timeout format
 * -------------------------------------------------------------------------*/

TEST(small_timeouts_encode_exactly)
{
    /* Up to 256 the mantissa holds the whole value, so nothing is lost. */
    static const uint32_t exact[] = { 1, 2, 3, 100, 255, 256 };

    for (unsigned i = 0; i < count_of_(exact); i++) {
        const uint16_t encoded = vl53l0x_encode_timeout(exact[i]);
        const uint32_t decoded = vl53l0x_decode_timeout(encoded);
        if (decoded != exact[i]) {
            printf("    %u encoded to 0x%04X and decoded to %u\n",
                   exact[i], encoded, decoded);
            CHECK(false);
            return;
        }
    }
}

TEST(a_large_timeout_is_never_encoded_longer_than_asked)
{
    /*
     * The property that matters. The format keeps eight bits of mantissa, so
     * anything above 256 loses precision — and it must lose it *downward*. A
     * timeout that came back longer would overrun the budget the caller set;
     * one that came back shorter merely ends the measurement early.
     */
    for (uint32_t mclks = 1; mclks < 100000u; mclks += 37u) {
        const uint32_t decoded = vl53l0x_decode_timeout(vl53l0x_encode_timeout(mclks));
        if (decoded > mclks) {
            printf("    %u encoded to something longer: %u\n", mclks, decoded);
            CHECK(false);
            return;
        }
    }
}

TEST(the_encoding_keeps_seven_bits_of_precision)
{
    /*
     * Lossy, but boundedly so: never worse than one part in 128.
     *
     * Not 256, which is the tempting guess. Shifting stops as soon as the
     * mantissa fits in a byte, so its top bit is always set and it lands
     * somewhere in 128..255 — which means the step between representable
     * values is up to 1/128 of the value, not 1/256.
     */
    for (uint32_t mclks = 257; mclks < 100000u; mclks += 53u) {
        const uint32_t decoded = vl53l0x_decode_timeout(vl53l0x_encode_timeout(mclks));
        const uint32_t lost = mclks - decoded;
        if (lost * 128u > mclks) {
            printf("    %u lost %u, more than one part in 128\n", mclks, lost);
            CHECK(false);
            return;
        }
    }
}

TEST(known_encodings_match)
{
    /* Worked through by hand: 1000 - 1 = 999, which needs two shifts to fit a
       byte, giving mantissa 249 and exponent 2; (249 << 2) + 1 = 997. */
    CHECK_EQ_INT(vl53l0x_encode_timeout(1000), 0x02F9);
    CHECK_EQ_U32(vl53l0x_decode_timeout(0x02F9), 997u);

    CHECK_EQ_INT(vl53l0x_encode_timeout(257), 0x0180);
    CHECK_EQ_U32(vl53l0x_decode_timeout(0x0180), 257u);
}

TEST(a_zero_timeout_encodes_to_zero)
{
    CHECK_EQ_INT(vl53l0x_encode_timeout(0), 0);
    /* And zero decodes to one macro clock, which is what the format says. */
    CHECK_EQ_U32(vl53l0x_decode_timeout(0), 1u);
}

/* ---------------------------------------------------------------------------
 * Microseconds and macro clocks
 * -------------------------------------------------------------------------*/

TEST(microseconds_and_macro_clocks_agree_both_ways)
{
    for (uint32_t us = 1000; us <= 200000u; us += 1000u) {
        const uint32_t mclks = vl53l0x_timeout_us_to_mclks(us, PRE_RANGE_VCSEL);
        const uint32_t back = vl53l0x_timeout_mclks_to_us((uint16_t)mclks,
                                                          PRE_RANGE_VCSEL);
        /* Within one macro period, which at 14 pclks is about 53 us. */
        const uint32_t difference = (back > us) ? back - us : us - back;
        if (difference > 60u) {
            printf("    %u us -> %u mclks -> %u us, off by %u\n", us, mclks, back,
                   difference);
            CHECK(false);
            return;
        }
    }
}

TEST(known_conversions_match)
{
    CHECK_EQ_U32(vl53l0x_timeout_us_to_mclks(20000, 14), 375u);
    CHECK_EQ_U32(vl53l0x_timeout_us_to_mclks(33000, 14), 618u);
    CHECK_EQ_U32(vl53l0x_timeout_mclks_to_us(375, 14), 20019u);
}

TEST(a_longer_vcsel_period_needs_fewer_macro_clocks)
{
    /* Same wall-clock timeout, longer macro period, so fewer of them. */
    const uint32_t at_14 = vl53l0x_timeout_us_to_mclks(33000, 14);
    const uint32_t at_18 = vl53l0x_timeout_us_to_mclks(33000, 18);
    CHECK(at_18 < at_14);
}

/* ---------------------------------------------------------------------------
 * The budget
 * -------------------------------------------------------------------------*/

static vl53l0x_sequence_timeouts_t default_timeouts(void)
{
    return (vl53l0x_sequence_timeouts_t){
        .pre_range_vcsel_period_pclks = 14,
        .final_range_vcsel_period_pclks = 14,
        .msrc_dss_tcc_us = 2000,
        .pre_range_us = 8000,
        .final_range_us = 20000,
    };
}

TEST(the_sequence_register_decodes_to_the_right_phases)
{
    /*
     * 0xE8 is what the driver restores after calibration: final range,
     * pre-range and DSS, with TCC and MSRC off. Worth writing out, because the
     * bits are not in the order the phases run.
     */
    const vl53l0x_sequence_steps_t steps = vl53l0x_decode_sequence_steps(0xE8);
    CHECK(steps.final_range);   /* bit 7 */
    CHECK(steps.pre_range);     /* bit 6 */
    CHECK(steps.dss);           /* bit 3 */
    CHECK(!steps.tcc);          /* bit 4, clear */
    CHECK(!steps.msrc);         /* bit 2, clear */

    /* 0xFF, which the driver sets during reference calibration, enables all. */
    const vl53l0x_sequence_steps_t all = vl53l0x_decode_sequence_steps(0xFF);
    CHECK(all.tcc && all.dss && all.msrc && all.pre_range && all.final_range);

    const vl53l0x_sequence_steps_t none = vl53l0x_decode_sequence_steps(0x00);
    CHECK(!none.tcc && !none.dss && !none.msrc && !none.pre_range && !none.final_range);
}

TEST(the_used_budget_includes_the_fixed_overheads)
{
    const vl53l0x_sequence_steps_t nothing = { 0 };
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();

    /* Even with no phases enabled there is a start and an end. */
    CHECK_EQ_U32(vl53l0x_budget_used_us(&nothing, &timeouts),
                 VL53L0X_START_OVERHEAD_US + VL53L0X_END_OVERHEAD_US);
}

TEST(dss_replaces_msrc_rather_than_adding_to_it)
{
    /*
     * The mistake worth guarding: they share the same measurement, so counting
     * both would over-count and make every reasonable budget look unaffordable.
     */
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();

    vl53l0x_sequence_steps_t with_dss = { .dss = true, .msrc = true };
    vl53l0x_sequence_steps_t dss_only = { .dss = true };

    CHECK_EQ_U32(vl53l0x_budget_used_us(&with_dss, &timeouts),
                 vl53l0x_budget_used_us(&dss_only, &timeouts));

    /* And DSS costs twice the measurement, unlike MSRC. */
    vl53l0x_sequence_steps_t msrc_only = { .msrc = true };
    CHECK(vl53l0x_budget_used_us(&dss_only, &timeouts) >
          vl53l0x_budget_used_us(&msrc_only, &timeouts));
}

TEST(the_final_range_gets_what_the_budget_leaves)
{
    const vl53l0x_sequence_steps_t steps = {
        .tcc = true, .dss = true, .pre_range = true, .final_range = true,
    };
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();

    uint32_t final_range_us = 0;
    CHECK(vl53l0x_final_range_timeout_us(&steps, &timeouts, 33000, &final_range_us));
    CHECK(final_range_us > 0);

    /* Feeding the answer back in gives a total that fits. */
    vl53l0x_sequence_timeouts_t applied = timeouts;
    applied.final_range_us = final_range_us;
    CHECK_EQ_U32(vl53l0x_budget_used_us(&steps, &applied), 33000u);
}

TEST(a_larger_budget_leaves_more_for_the_final_range)
{
    const vl53l0x_sequence_steps_t steps = {
        .tcc = true, .dss = true, .pre_range = true, .final_range = true,
    };
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();

    uint32_t at_33 = 0;
    uint32_t at_200 = 0;
    CHECK(vl53l0x_final_range_timeout_us(&steps, &timeouts, 33000, &at_33));
    CHECK(vl53l0x_final_range_timeout_us(&steps, &timeouts, 200000, &at_200));
    CHECK(at_200 > at_33);
    CHECK_EQ_U32(at_200 - at_33, 200000u - 33000u);
}

TEST(a_budget_that_cannot_be_met_is_refused_not_clamped)
{
    /*
     * Silently clamping would be the worst outcome: a budget that was ignored
     * is indistinguishable from one that was honoured until the readings come
     * back wrong.
     */
    const vl53l0x_sequence_steps_t steps = {
        .tcc = true, .dss = true, .pre_range = true, .final_range = true,
    };
    vl53l0x_sequence_timeouts_t expensive = default_timeouts();
    expensive.msrc_dss_tcc_us = 30000;
    expensive.pre_range_us = 30000;

    uint32_t final_range_us = 12345;
    CHECK(!vl53l0x_final_range_timeout_us(&steps, &expensive, 33000, &final_range_us));
}

TEST(a_budget_below_the_minimum_is_refused)
{
    const vl53l0x_sequence_steps_t steps = { .final_range = true };
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();

    uint32_t final_range_us = 0;
    CHECK(!vl53l0x_final_range_timeout_us(&steps, &timeouts,
                                          VL53L0X_MIN_TIMING_BUDGET_US - 1,
                                          &final_range_us));
    CHECK(vl53l0x_final_range_timeout_us(&steps, &timeouts,
                                         VL53L0X_MIN_TIMING_BUDGET_US,
                                         &final_range_us));
}

TEST(null_arguments_are_refused)
{
    const vl53l0x_sequence_steps_t steps = { .final_range = true };
    const vl53l0x_sequence_timeouts_t timeouts = default_timeouts();
    uint32_t final_range_us = 0;

    CHECK_EQ_U32(vl53l0x_budget_used_us(NULL, &timeouts), 0u);
    CHECK_EQ_U32(vl53l0x_budget_used_us(&steps, NULL), 0u);
    CHECK(!vl53l0x_final_range_timeout_us(NULL, &timeouts, 33000, &final_range_us));
    CHECK(!vl53l0x_final_range_timeout_us(&steps, &timeouts, 33000, NULL));
}

TEST_MAIN(
    RUN(the_vcsel_period_register_holds_half_the_period_less_one);
    RUN(every_even_vcsel_period_round_trips);
    RUN(the_macro_period_matches_the_datasheet_formula);
    RUN(a_longer_vcsel_period_gives_a_longer_macro_period);

    RUN(small_timeouts_encode_exactly);
    RUN(a_large_timeout_is_never_encoded_longer_than_asked);
    RUN(the_encoding_keeps_seven_bits_of_precision);
    RUN(known_encodings_match);
    RUN(a_zero_timeout_encodes_to_zero);

    RUN(microseconds_and_macro_clocks_agree_both_ways);
    RUN(known_conversions_match);
    RUN(a_longer_vcsel_period_needs_fewer_macro_clocks);

    RUN(the_sequence_register_decodes_to_the_right_phases);
    RUN(the_used_budget_includes_the_fixed_overheads);
    RUN(dss_replaces_msrc_rather_than_adding_to_it);
    RUN(the_final_range_gets_what_the_budget_leaves);
    RUN(a_larger_budget_leaves_more_for_the_final_range);
    RUN(a_budget_that_cannot_be_met_is_refused_not_clamped);
    RUN(a_budget_below_the_minimum_is_refused);
    RUN(null_arguments_are_refused);
)
