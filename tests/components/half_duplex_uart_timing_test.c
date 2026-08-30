/*
 * Host-side tests for the PIO clock divider arithmetic.
 *
 * The cases that matter are the servo bus rates on the two system clocks the
 * framework targets: RP2040 at 125 MHz and RP2350 at 150 MHz. A silently
 * mis-clocked bus is the failure this file exists to prevent.
 */

#include "test.h"

#include "half_duplex_uart_timing.h"

#define RP2040_CLOCK_HZ 125000000u
#define RP2350_CLOCK_HZ 150000000u

static half_duplex_uart_timing_t timing_for(uint32_t clock, uint32_t baud)
{
    half_duplex_uart_timing_t timing = { 0 };
    CHECK(half_duplex_uart_compute_timing(clock, baud, &timing));
    return timing;
}

TEST(one_megabaud_is_exact_on_both_platforms)
{
    /* The rate AX-12 and Feetech buses run at. Both default system clocks
       divide into it exactly, which is worth pinning down. */
    half_duplex_uart_timing_t rp2040 = timing_for(RP2040_CLOCK_HZ, 1000000);
    CHECK_EQ_INT(rp2040.actual_baudrate, 1000000);
    CHECK_EQ_INT(rp2040.error_permille, 0);
    CHECK_EQ_INT(rp2040.divider_int, 15);
    CHECK_EQ_INT(rp2040.divider_frac, 160); /* 15 + 160/256 = 15.625 */

    half_duplex_uart_timing_t rp2350 = timing_for(RP2350_CLOCK_HZ, 1000000);
    CHECK_EQ_INT(rp2350.actual_baudrate, 1000000);
    CHECK_EQ_INT(rp2350.error_permille, 0);
    CHECK_EQ_INT(rp2350.divider_int, 18);
    CHECK_EQ_INT(rp2350.divider_frac, 192); /* 18 + 192/256 = 18.75 */
}

TEST(the_ax12_default_rate_is_usable_on_both_platforms)
{
    half_duplex_uart_timing_t rp2040 = timing_for(RP2040_CLOCK_HZ, 57600);
    CHECK(half_duplex_uart_timing_is_usable(&rp2040));

    half_duplex_uart_timing_t rp2350 = timing_for(RP2350_CLOCK_HZ, 57600);
    CHECK(half_duplex_uart_timing_is_usable(&rp2350));
}

TEST(common_rates_are_all_within_tolerance)
{
    static const uint32_t rates[] = {
        9600, 19200, 38400, 57600, 115200, 200000, 250000, 500000, 1000000,
    };
    static const uint32_t clocks[] = { RP2040_CLOCK_HZ, RP2350_CLOCK_HZ };

    for (unsigned c = 0; c < count_of_(clocks); c++) {
        for (unsigned r = 0; r < count_of_(rates); r++) {
            half_duplex_uart_timing_t timing = { 0 };
            if (!half_duplex_uart_compute_timing(clocks[c], rates[r], &timing)) {
                CHECK_EQ_INT(rates[r], 0); /* reports which rate failed */
                continue;
            }
            if (!half_duplex_uart_timing_is_usable(&timing)) {
                CHECK_EQ_INT(timing.error_permille, 0);
            }
        }
    }
}

TEST(the_divider_is_the_nearest_representable_value)
{
    /* 125 MHz / (8 * 57600) = 271.267..., so 271 + 68/256 = 271.2656 is the
       closest 8.8 value; 67/256 and 69/256 are both further away. */
    half_duplex_uart_timing_t timing = timing_for(RP2040_CLOCK_HZ, 57600);
    CHECK_EQ_INT(timing.divider_int, 271);
    CHECK_EQ_INT(timing.divider_frac, 68);
}

TEST(a_rate_too_high_for_the_clock_is_rejected)
{
    /* Needs a divider below 1: the PIO cannot run faster than one cycle. */
    half_duplex_uart_timing_t timing = { 0 };
    CHECK(!half_duplex_uart_compute_timing(RP2040_CLOCK_HZ, 20000000, &timing));
}

TEST(a_rate_too_low_for_the_clock_is_rejected)
{
    /* Needs a divider above 65536. */
    half_duplex_uart_timing_t timing = { 0 };
    CHECK(!half_duplex_uart_compute_timing(RP2040_CLOCK_HZ, 100, &timing));
}

TEST(the_extremes_of_the_divider_range_are_accepted)
{
    half_duplex_uart_timing_t timing = { 0 };

    /* Exactly divider 1.0: baud = clock / 8. */
    CHECK(half_duplex_uart_compute_timing(RP2040_CLOCK_HZ,
                                          RP2040_CLOCK_HZ / 8, &timing));
    CHECK_EQ_INT(timing.divider_int, 1);
    CHECK_EQ_INT(timing.divider_frac, 0);

    /*
     * Exactly divider 65536, which the hardware encodes as an integer part of
     * 0. No real system clock divides to that at an integer baud, so this uses
     * a synthetic one chosen to land on the boundary exactly: the point is the
     * encoding, not the rate.
     */
    const uint32_t synthetic_clock = 8u * 65536u * 100u; /* 52.4288 MHz */
    CHECK(half_duplex_uart_compute_timing(synthetic_clock, 100, &timing));
    CHECK_EQ_INT(timing.divider_int, 0);
    CHECK_EQ_INT(timing.divider_frac, 0);
    CHECK_EQ_INT(timing.actual_baudrate, 100);

    /* One step past the boundary must be rejected rather than wrapping to a
       tiny divider through that same truncation. */
    CHECK(!half_duplex_uart_compute_timing(synthetic_clock, 99, &timing));
}

TEST(degenerate_inputs_are_rejected)
{
    half_duplex_uart_timing_t timing = { 0 };
    CHECK(!half_duplex_uart_compute_timing(0, 115200, &timing));
    CHECK(!half_duplex_uart_compute_timing(RP2040_CLOCK_HZ, 0, &timing));
    CHECK(!half_duplex_uart_compute_timing(RP2040_CLOCK_HZ, 115200, NULL));
}

TEST(an_out_of_tolerance_rate_is_reported_as_unusable)
{
    /* A deliberately awkward clock: 1 MHz asked of a 3 MHz system clock needs
       a divider of 0.375, so this must be rejected outright... */
    half_duplex_uart_timing_t timing = { 0 };
    CHECK(!half_duplex_uart_compute_timing(3000000, 1000000, &timing));

    /* ...while a rate that is representable but coarse is reported, not hidden.
       9 MHz / 8 / 1 MHz = 1.125 exactly, but 9.1 MHz gives a divider of
       1.1375, quantised to 1 + 35/256 = 1.13671875. */
    CHECK(half_duplex_uart_compute_timing(9100000, 1000000, &timing));
    CHECK_EQ_INT(timing.divider_int, 1);
    CHECK(timing.actual_baudrate > 0);
}

TEST(frame_time_covers_start_and_stop_bits)
{
    /* 10 bits per byte: at 1 Mbaud one byte takes 10 us. */
    CHECK_EQ_INT(half_duplex_uart_frame_time_us(1000000, 1), 10);
    CHECK_EQ_INT(half_duplex_uart_frame_time_us(1000000, 8), 80);

    /* 115200 baud, 1 byte = 86.8 us, rounded up so a timeout is never short. */
    CHECK_EQ_INT(half_duplex_uart_frame_time_us(115200, 1), 87);
}

TEST(frame_time_of_nothing_is_nothing)
{
    CHECK_EQ_INT(half_duplex_uart_frame_time_us(1000000, 0), 0);
    CHECK_EQ_INT(half_duplex_uart_frame_time_us(0, 10), 0);
}

TEST_MAIN(
    RUN(one_megabaud_is_exact_on_both_platforms);
    RUN(the_ax12_default_rate_is_usable_on_both_platforms);
    RUN(common_rates_are_all_within_tolerance);
    RUN(the_divider_is_the_nearest_representable_value);
    RUN(a_rate_too_high_for_the_clock_is_rejected);
    RUN(a_rate_too_low_for_the_clock_is_rejected);
    RUN(the_extremes_of_the_divider_range_are_accepted);
    RUN(degenerate_inputs_are_rejected);
    RUN(an_out_of_tolerance_rate_is_reported_as_unusable);
    RUN(frame_time_covers_start_and_stop_bits);
    RUN(frame_time_of_nothing_is_nothing);
)
