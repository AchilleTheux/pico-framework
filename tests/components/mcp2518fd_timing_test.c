#include "test.h"
#include "mcp2518fd_timing.h"

static void check_phase(uint32_t clock, uint32_t bitrate, uint32_t reg, bool data)
{
    uint32_t brp = (reg >> 24) + 1;
    uint32_t seg1 = ((reg >> 16) & 255) + 1;
    uint32_t seg2 = ((reg >> 8) & 255) + 1;
    uint32_t sjw = (reg & 255) + 1;
    CHECK_EQ_U32(bitrate * brp * (1 + seg1 + seg2), clock);
    CHECK(seg1 <= (data ? 32u : 256u));
    CHECK(seg2 <= (data ? 16u : 128u));
    CHECK(sjw <= seg2);
}

TEST(reference_manual_example)
{
    /* DS20005678E table 3-5, 40 MHz / 500k / 2M, 80% sample points. */
    mcp2518fd_bit_timing_t timing;
    CHECK(mcp2518fd_compute_bit_timing(40000000, 500000, 2000000, &timing));
    CHECK_EQ_U32(timing.nbtcfg, 0x003e0f0f);
    CHECK_EQ_U32(timing.dbtcfg, 0x000e0303);
    CHECK_EQ_U32(timing.tdc, 0x00020f00);
}

TEST(rates)
{
    const uint32_t nominal[] = {10000, 20000, 50000, 100000, 125000, 250000, 500000, 1000000};
    const uint32_t data[] = {1000000, 2000000, 4000000, 5000000, 8000000};
    for (uint32_t clock = 20000000; clock <= 40000000; clock += 20000000) {
        for (unsigned i = 0; i < sizeof(nominal) / sizeof(nominal[0]); i++) {
            mcp2518fd_bit_timing_t timing;
            CHECK(mcp2518fd_compute_bit_timing(clock, nominal[i], 0, &timing));
            check_phase(clock, nominal[i], timing.nbtcfg, false);
            CHECK_EQ_U32(timing.tdc, 0);
            for (unsigned j = 0; j < sizeof(data) / sizeof(data[0]); j++) {
                if (clock % data[j] != 0) continue;
                CHECK(mcp2518fd_compute_bit_timing(clock, nominal[i], data[j], &timing));
                check_phase(clock, data[j], timing.dbtcfg, true);
            }
        }
    }
}

TEST(invalid_settings)
{
    mcp2518fd_bit_timing_t timing = {.nbtcfg = 0x1234};
    CHECK(!mcp2518fd_compute_bit_timing(0, 500000, 0, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(16000000, 500000, 0, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 0, 0, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 2000000, 0, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 333333, 0, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 500000, 250000, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 500000, 10000000, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(20000000, 500000, 8000000, &timing));
    CHECK(!mcp2518fd_compute_bit_timing(40000000, 500000, 0, NULL));
    CHECK_EQ_U32(timing.nbtcfg, 0x1234);
}

TEST_MAIN(RUN(reference_manual_example); RUN(rates); RUN(invalid_settings);)
