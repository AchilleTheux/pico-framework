#include <stddef.h>

#include "half_duplex_uart_timing.h"

/* The divider is held in 1/256ths throughout, which is the hardware's own
   resolution, so the only rounding is the single deliberate one below. */
#define DIVIDER_FRACTION_STEPS 256u

/* An 8.8 divider spans 1.0 to 65536.0 inclusive. */
#define DIVIDER_256_MIN (1u * DIVIDER_FRACTION_STEPS)
#define DIVIDER_256_MAX (65536u * DIVIDER_FRACTION_STEPS)

bool half_duplex_uart_compute_timing(uint32_t sys_clock_hz, uint32_t baudrate,
                                     half_duplex_uart_timing_t *out)
{
    if (out == NULL || sys_clock_hz == 0 || baudrate == 0) {
        return false;
    }

    const uint64_t cycles_per_second =
        (uint64_t)baudrate * HALF_DUPLEX_UART_CYCLES_PER_BIT;

    /* divider_256 = round(sys_clock / cycles_per_second * 256) */
    const uint64_t numerator = (uint64_t)sys_clock_hz * DIVIDER_FRACTION_STEPS;
    const uint64_t divider_256 =
        (numerator + cycles_per_second / 2) / cycles_per_second;

    if (divider_256 < DIVIDER_256_MIN || divider_256 > DIVIDER_256_MAX) {
        return false;
    }

    /* 65536 is encoded as an integer part of 0, which is why this truncates
       into a uint16_t deliberately rather than clamping. */
    out->divider_int = (uint16_t)(divider_256 / DIVIDER_FRACTION_STEPS);
    out->divider_frac = (uint8_t)(divider_256 % DIVIDER_FRACTION_STEPS);

    const uint64_t actual =
        (numerator + (divider_256 * HALF_DUPLEX_UART_CYCLES_PER_BIT) / 2) /
        (divider_256 * HALF_DUPLEX_UART_CYCLES_PER_BIT);
    out->actual_baudrate = (uint32_t)actual;

    const int64_t error = ((int64_t)actual - (int64_t)baudrate) * 1000;
    out->error_permille = (int32_t)(error / (int64_t)baudrate);

    return true;
}

bool half_duplex_uart_timing_is_usable(const half_duplex_uart_timing_t *timing)
{
    if (timing == NULL) {
        return false;
    }

    const int32_t magnitude = timing->error_permille < 0 ? -timing->error_permille
                                                         : timing->error_permille;
    return magnitude <= HALF_DUPLEX_UART_MAX_ERROR_PERMILLE;
}

uint32_t half_duplex_uart_frame_time_us(uint32_t baudrate, uint32_t bytes)
{
    if (baudrate == 0) {
        return 0;
    }

    /* 10 bits per 8N1 frame, rounded up so a timeout is never short. */
    const uint64_t bits = (uint64_t)bytes * 10u;
    return (uint32_t)((bits * 1000000u + baudrate - 1) / baudrate);
}
