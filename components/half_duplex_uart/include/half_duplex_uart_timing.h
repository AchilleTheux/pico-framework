/*
 * half_duplex_uart_timing - PIO clock divider arithmetic.
 *
 * Separated from the driver because it is pure integer maths and worth getting
 * exactly right: the PIO clock divider is 8.8 fixed point, so most requested
 * baud rates cannot be hit exactly. A UART tolerates roughly 2% of cumulative
 * error over a 10-bit frame before it starts sampling the wrong bit, and a bus
 * clocked slightly wrong fails intermittently rather than obviously — the kind
 * of fault that costs an afternoon.
 *
 * No Pico SDK dependency, so this is unit-tested on the host.
 */

#ifndef PICO_FRAMEWORK_HALF_DUPLEX_UART_TIMING_H
#define PICO_FRAMEWORK_HALF_DUPLEX_UART_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The PIO programs spend 8 execution cycles per bit. */
#define HALF_DUPLEX_UART_CYCLES_PER_BIT 8u

/*
 * Largest baud error the driver accepts, in parts per thousand. 2% is the
 * usual working limit for 8N1: by the stop bit the sampling point has drifted
 * a fifth of a bit, which still lands inside the eye.
 */
#ifndef HALF_DUPLEX_UART_MAX_ERROR_PERMILLE
#define HALF_DUPLEX_UART_MAX_ERROR_PERMILLE 20
#endif

typedef struct {
    /* As accepted by sm_config_set_clkdiv_int_frac(): the effective divider is
       `integer + fraction / 256`, and an integer part of 0 means 65536. */
    uint16_t divider_int;
    uint8_t divider_frac;

    /* What the bus will actually run at, after 8.8 quantisation. */
    uint32_t actual_baudrate;

    /* (actual - requested) / requested, in parts per thousand. Signed: a
       negative value means the link is running slow. */
    int32_t error_permille;
} half_duplex_uart_timing_t;

/*
 * Compute the divider for `baudrate` from a system clock of `sys_clock_hz`.
 *
 * Returns false when the rate is unreachable — the exact divider would fall
 * below 1 (baud too high for the system clock) or above 65536 (too low). The
 * result is filled in whenever true is returned, including when the error
 * exceeds the tolerance; use half_duplex_uart_timing_is_usable() to decide.
 */
bool half_duplex_uart_compute_timing(uint32_t sys_clock_hz, uint32_t baudrate,
                                     half_duplex_uart_timing_t *out);

/* True when the quantised rate is within HALF_DUPLEX_UART_MAX_ERROR_PERMILLE. */
bool half_duplex_uart_timing_is_usable(const half_duplex_uart_timing_t *timing);

/*
 * Time on the wire for `bytes` 8N1 frames at `baudrate`, in microseconds.
 * Each frame is 10 bits: start, 8 data, stop. Used to size transfer timeouts.
 */
uint32_t half_duplex_uart_frame_time_us(uint32_t baudrate, uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_HALF_DUPLEX_UART_TIMING_H */
