/* Host-side tests for MCP2515 bit-timing register computation. */

#include "test.h"

#include "mcp2515_timing.h"

/* Reconstruct the bit period the registers actually produce and compare it
   against 1/bitrate exactly, in integer oscillator-cycle units — this is
   the property that matters, independent of exactly how CNF2/CNF3 split
   PRSEG/PHSEG1/PHSEG2. */
static uint32_t reconstruct_ntq_times_brp1(const mcp2515_bit_timing_t *timing)
{
    const uint32_t brp = timing->cnf1 & 0x3Fu;
    const uint32_t prseg = (timing->cnf2 & 0x07u) + 1u;
    const uint32_t phseg1 = ((timing->cnf2 >> 3) & 0x07u) + 1u;
    const uint32_t phseg2 = (timing->cnf3 & 0x07u) + 1u;
    const uint32_t ntq = 1u /* sync */ + prseg + phseg1 + phseg2;
    return ntq * (brp + 1u);
}

TEST(a_common_16mhz_500kbps_configuration_is_exact)
{
    mcp2515_bit_timing_t timing;
    CHECK(mcp2515_compute_bit_timing(16000000u, 500000u, &timing));
    /* oscillator_hz / (2 * bitrate) = 16 */
    CHECK_EQ_U32(reconstruct_ntq_times_brp1(&timing), 16u);
}

TEST(an_8mhz_125kbps_configuration_is_exact)
{
    mcp2515_bit_timing_t timing;
    CHECK(mcp2515_compute_bit_timing(8000000u, 125000u, &timing));
    /* oscillator_hz / (2 * bitrate) = 32 */
    CHECK_EQ_U32(reconstruct_ntq_times_brp1(&timing), 32u);
}

TEST(every_register_field_stays_within_its_valid_range)
{
    /* Oscillator/bitrate pairs spanning small and large targets. */
    static const struct {
        uint32_t oscillator_hz;
        uint32_t bitrate;
    } cases[] = {
        {8000000u, 125000u},  {8000000u, 250000u},  {8000000u, 500000u},
        {16000000u, 125000u}, {16000000u, 250000u}, {16000000u, 500000u},
        {16000000u, 1000000u}, {20000000u, 500000u}, {20000000u, 1000000u},
    };

    for (size_t i = 0; i < count_of_(cases); i++) {
        mcp2515_bit_timing_t timing;
        if (!mcp2515_compute_bit_timing(cases[i].oscillator_hz, cases[i].bitrate, &timing)) {
            continue; /* not every pair is exactly achievable; that is fine */
        }

        const uint32_t brp = timing.cnf1 & 0x3Fu;
        const uint32_t sjw = (timing.cnf1 >> 6) & 0x03u;
        const uint32_t prseg = timing.cnf2 & 0x07u;
        const uint32_t phseg1 = (timing.cnf2 >> 3) & 0x07u;
        const uint32_t btlmode = (timing.cnf2 >> 7) & 0x01u;
        const uint32_t phseg2 = timing.cnf3 & 0x07u;

        CHECK(brp <= 63u);
        CHECK(sjw == 0u); /* this implementation always picks SJW = 1 TQ */
        CHECK_EQ_U32(btlmode, 1u); /* PHSEG2 is always explicitly set by CNF3 */

        const uint32_t ntq = 1u + (prseg + 1u) + (phseg1 + 1u) + (phseg2 + 1u);
        CHECK(ntq >= 8u);
        CHECK(ntq <= 25u);

        /* The exact bit-period identity mcp2515_compute_bit_timing() must
           satisfy for any oscillator/bitrate pair it accepts. */
        CHECK_EQ_U32(ntq * (brp + 1u), cases[i].oscillator_hz / (2u * cases[i].bitrate));
    }
}

TEST(an_unreachable_bitrate_is_rejected_rather_than_approximated)
{
    mcp2515_bit_timing_t timing;
    /* 8 MHz cannot reach 1 Mbit/s: the minimum 8 TQ/bit would need BRP+1 to
       be a fraction. */
    CHECK(!mcp2515_compute_bit_timing(8000000u, 1000000u, &timing));
    /* An oscillator that is not an exact multiple of 2*bitrate at all. */
    CHECK(!mcp2515_compute_bit_timing(8000000u, 83333u, &timing));
}

TEST(invalid_arguments_are_rejected)
{
    mcp2515_bit_timing_t timing;
    CHECK(!mcp2515_compute_bit_timing(0, 500000u, &timing));
    CHECK(!mcp2515_compute_bit_timing(8000000u, 0, &timing));
    CHECK(!mcp2515_compute_bit_timing(8000000u, 500000u, NULL));
}

TEST_MAIN(
    RUN(a_common_16mhz_500kbps_configuration_is_exact);
    RUN(an_8mhz_125kbps_configuration_is_exact);
    RUN(every_register_field_stays_within_its_valid_range);
    RUN(an_unreachable_bitrate_is_rejected_rather_than_approximated);
    RUN(invalid_arguments_are_rejected);
)
