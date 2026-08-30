#include "half_duplex_uart.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include "half_duplex_uart.pio.h"

/* ---------------------------------------------------------------------------
 * State machine setup
 * -------------------------------------------------------------------------*/

static void apply_timing(const half_duplex_uart_t *bus)
{
    pio_sm_set_clkdiv_int_frac(bus->pio, bus->sm_tx,
                               bus->timing.divider_int, bus->timing.divider_frac);
    pio_sm_set_clkdiv_int_frac(bus->pio, bus->sm_rx,
                               bus->timing.divider_int, bus->timing.divider_frac);
}

static void configure_tx(half_duplex_uart_t *bus)
{
    /*
     * The direction signal is side-set, and side-set has to map somewhere. On
     * a board with no transceiver it maps to the data pin itself, where it is
     * harmless: the program only side-sets while it also owns the pin
     * direction, and the values it drives (idle high, released low) match what
     * the line does anyway.
     */
    const uint side_set_pin = (bus->direction_pin >= 0) ? (uint)bus->direction_pin
                                                        : bus->pin;

    pio_gpio_init(bus->pio, bus->pin);
    if (bus->direction_pin >= 0) {
        pio_gpio_init(bus->pio, (uint)bus->direction_pin);
        pio_sm_set_consecutive_pindirs(bus->pio, bus->sm_tx,
                                       (uint)bus->direction_pin, 1, true);
    }

    /* Start released: the program takes the line only while sending a byte. */
    pio_sm_set_consecutive_pindirs(bus->pio, bus->sm_tx, bus->pin, 1, false);

    pio_sm_config c = half_duplex_uart_tx_program_get_default_config(bus->offset_tx);
    sm_config_set_out_pins(&c, bus->pin, 1);
    sm_config_set_set_pins(&c, bus->pin, 1);
    sm_config_set_sideset_pins(&c, side_set_pin);

    /* LSB first, no autopull: the program pulls one byte at a time. */
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(bus->pio, bus->sm_tx, bus->offset_tx, &c);
}

static void configure_rx(half_duplex_uart_t *bus)
{
    /* The idle level of a released open bus. Without it the receiver sees a
       floating pin as a stream of start bits. */
    gpio_pull_up(bus->pin);

    pio_sm_set_consecutive_pindirs(bus->pio, bus->sm_rx, bus->pin, 1, false);

    pio_sm_config c = half_duplex_uart_rx_program_get_default_config(bus->offset_rx);
    sm_config_set_in_pins(&c, bus->pin);   /* for WAIT and IN */
    sm_config_set_jmp_pin(&c, bus->pin);   /* for the stop-bit check */

    /* LSB first, no autopush: the program pushes only well-framed bytes, which
       land left-justified in the FIFO word. */
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    pio_sm_init(bus->pio, bus->sm_rx, bus->offset_rx, &c);
}

half_duplex_uart_result_t half_duplex_uart_init(half_duplex_uart_t *bus,
                                                const half_duplex_uart_config_t *config)
{
    if (bus == NULL || config == NULL || config->pio == NULL || config->baudrate == 0) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }
    if (config->direction_pin >= 0 && (uint)config->direction_pin == config->pin) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }

    half_duplex_uart_timing_t timing;
    if (!half_duplex_uart_compute_timing(clock_get_hz(clk_sys), config->baudrate, &timing) ||
        !half_duplex_uart_timing_is_usable(&timing)) {
        return HALF_DUPLEX_UART_ERR_BAUDRATE;
    }

    const int offset_tx = pio_add_program(config->pio, &half_duplex_uart_tx_program);
    if (offset_tx < 0) {
        return HALF_DUPLEX_UART_ERR_NO_PROGRAM_SPACE;
    }

    const int offset_rx = pio_add_program(config->pio, &half_duplex_uart_rx_program);
    if (offset_rx < 0) {
        pio_remove_program(config->pio, &half_duplex_uart_tx_program, (uint)offset_tx);
        return HALF_DUPLEX_UART_ERR_NO_PROGRAM_SPACE;
    }

    const int sm_tx = pio_claim_unused_sm(config->pio, false);
    const int sm_rx = pio_claim_unused_sm(config->pio, false);
    if (sm_tx < 0 || sm_rx < 0) {
        if (sm_tx >= 0) {
            pio_sm_unclaim(config->pio, (uint)sm_tx);
        }
        if (sm_rx >= 0) {
            pio_sm_unclaim(config->pio, (uint)sm_rx);
        }
        pio_remove_program(config->pio, &half_duplex_uart_rx_program, (uint)offset_rx);
        pio_remove_program(config->pio, &half_duplex_uart_tx_program, (uint)offset_tx);
        return HALF_DUPLEX_UART_ERR_NO_STATE_MACHINE;
    }

    *bus = (half_duplex_uart_t){
        .pio = config->pio,
        .sm_tx = (uint)sm_tx,
        .sm_rx = (uint)sm_rx,
        .offset_tx = (uint)offset_tx,
        .offset_rx = (uint)offset_rx,
        .pin = config->pin,
        .direction_pin = config->direction_pin,
        .baudrate = config->baudrate,
        .receives_own_transmission = config->receives_own_transmission,
        .timing = timing,
        .initialised = true,
    };

    configure_tx(bus);
    configure_rx(bus);
    apply_timing(bus);

    pio_sm_set_enabled(bus->pio, bus->sm_tx, true);
    pio_sm_set_enabled(bus->pio, bus->sm_rx, true);

    return HALF_DUPLEX_UART_OK;
}

void half_duplex_uart_deinit(half_duplex_uart_t *bus)
{
    if (bus == NULL || !bus->initialised) {
        return;
    }

    pio_sm_set_enabled(bus->pio, bus->sm_tx, false);
    pio_sm_set_enabled(bus->pio, bus->sm_rx, false);
    pio_sm_unclaim(bus->pio, bus->sm_tx);
    pio_sm_unclaim(bus->pio, bus->sm_rx);
    pio_remove_program(bus->pio, &half_duplex_uart_rx_program, bus->offset_rx);
    pio_remove_program(bus->pio, &half_duplex_uart_tx_program, bus->offset_tx);

    bus->initialised = false;
}

half_duplex_uart_result_t half_duplex_uart_set_baudrate(half_duplex_uart_t *bus,
                                                        uint32_t baudrate)
{
    if (bus == NULL || !bus->initialised || baudrate == 0) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }

    half_duplex_uart_timing_t timing;
    if (!half_duplex_uart_compute_timing(clock_get_hz(clk_sys), baudrate, &timing) ||
        !half_duplex_uart_timing_is_usable(&timing)) {
        return HALF_DUPLEX_UART_ERR_BAUDRATE;
    }

    /* Stop both machines before retiming so neither is left mid-frame at the
       old rate, and restart them from the top of their programs. */
    pio_sm_set_enabled(bus->pio, bus->sm_tx, false);
    pio_sm_set_enabled(bus->pio, bus->sm_rx, false);

    bus->baudrate = baudrate;
    bus->timing = timing;
    apply_timing(bus);

    pio_sm_clear_fifos(bus->pio, bus->sm_tx);
    pio_sm_clear_fifos(bus->pio, bus->sm_rx);
    pio_sm_restart(bus->pio, bus->sm_tx);
    pio_sm_restart(bus->pio, bus->sm_rx);
    pio_sm_exec(bus->pio, bus->sm_tx, pio_encode_jmp(bus->offset_tx));
    pio_sm_exec(bus->pio, bus->sm_rx, pio_encode_jmp(bus->offset_rx));

    pio_sm_set_enabled(bus->pio, bus->sm_tx, true);
    pio_sm_set_enabled(bus->pio, bus->sm_rx, true);

    return HALF_DUPLEX_UART_OK;
}

const half_duplex_uart_timing_t *half_duplex_uart_get_timing(const half_duplex_uart_t *bus)
{
    return (bus != NULL && bus->initialised) ? &bus->timing : NULL;
}

/* ---------------------------------------------------------------------------
 * Receive
 * -------------------------------------------------------------------------*/

/* The RX program shifts right into a 32-bit ISR and pushes at the stop bit, so
   the byte ends up in the top 8 bits of the FIFO word. */
static uint8_t rx_byte(half_duplex_uart_t *bus)
{
    return (uint8_t)(pio_sm_get(bus->pio, bus->sm_rx) >> 24);
}

void half_duplex_uart_flush_rx(half_duplex_uart_t *bus)
{
    if (bus == NULL || !bus->initialised) {
        return;
    }
    pio_sm_clear_fifos(bus->pio, bus->sm_rx);
}

half_duplex_uart_result_t half_duplex_uart_read(half_duplex_uart_t *bus,
                                                uint8_t *buffer, size_t capacity,
                                                size_t *received,
                                                uint32_t timeout_us,
                                                uint32_t inter_byte_timeout_us)
{
    if (received != NULL) {
        *received = 0;
    }
    if (bus == NULL || !bus->initialised || buffer == NULL) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }
    if (capacity == 0) {
        return HALF_DUPLEX_UART_OK;
    }

    size_t count = 0;
    absolute_time_t deadline = make_timeout_time_us(timeout_us);

    while (count < capacity) {
        if (pio_sm_is_rx_fifo_empty(bus->pio, bus->sm_rx)) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
                break;
            }
            tight_loop_contents();
            continue;
        }

        buffer[count] = rx_byte(bus);
        count++;

        /* Once the reply has started, the gap between its bytes is the thing
           worth bounding: a servo answers back to back, so a quiet line means
           the message ended. */
        deadline = make_timeout_time_us(inter_byte_timeout_us);
    }

    if (received != NULL) {
        *received = count;
    }
    return count > 0 ? HALF_DUPLEX_UART_OK : HALF_DUPLEX_UART_ERR_TIMEOUT;
}

half_duplex_uart_result_t half_duplex_uart_read_exact(half_duplex_uart_t *bus,
                                                      uint8_t *buffer, size_t len,
                                                      uint32_t timeout_us)
{
    if (bus == NULL || !bus->initialised || buffer == NULL) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return HALF_DUPLEX_UART_OK;
    }

    /*
     * A reply of known length has no meaningful inter-byte deadline of its
     * own: allow the whole timeout for the whole message, so a slow first byte
     * does not make a correct reply look truncated.
     */
    size_t received = 0;
    const half_duplex_uart_result_t result =
        half_duplex_uart_read(bus, buffer, len, &received, timeout_us, timeout_us);

    if (result != HALF_DUPLEX_UART_OK) {
        return result;
    }
    return received == len ? HALF_DUPLEX_UART_OK : HALF_DUPLEX_UART_ERR_TIMEOUT;
}

/* ---------------------------------------------------------------------------
 * Transmit
 * -------------------------------------------------------------------------*/

half_duplex_uart_result_t half_duplex_uart_write(half_duplex_uart_t *bus,
                                                 const uint8_t *data, size_t len)
{
    if (bus == NULL || !bus->initialised || (data == NULL && len > 0)) {
        return HALF_DUPLEX_UART_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return HALF_DUPLEX_UART_OK;
    }

    /* Anything still in the RX FIFO predates this transaction. */
    pio_sm_clear_fifos(bus->pio, bus->sm_rx);

    const uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + bus->sm_tx);

    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(bus->pio, bus->sm_tx, data[i]);
    }

    /*
     * put_blocking only means the FIFO took the word. The transmit program
     * stalls on its `pull` when it runs out of work, and that stall is also
     * where it releases the line, so waiting for it is exactly waiting for the
     * bus to be free. Clear the sticky flag first: an already-idle machine
     * re-asserts it, because it keeps retrying the pull.
     */
    bus->pio->fdebug = stall_mask;
    while ((bus->pio->fdebug & stall_mask) == 0) {
        tight_loop_contents();
    }

    if (!bus->receives_own_transmission) {
        return HALF_DUPLEX_UART_OK;
    }

    /*
     * Our own bytes came back on the shared wire. Wait for them rather than
     * clearing the FIFO blindly: the last one may still be in flight, and
     * dropping it later would eat the first byte of the reply instead.
     */
    const uint32_t echo_timeout_us =
        half_duplex_uart_frame_time_us(bus->baudrate, 2) + 100u;

    for (size_t i = 0; i < len; i++) {
        const absolute_time_t deadline = make_timeout_time_us(echo_timeout_us);
        while (pio_sm_is_rx_fifo_empty(bus->pio, bus->sm_rx)) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
                /* The echo never arrived: the wiring is not what the config
                   says, or the line is being held by something else. Drop what
                   did arrive so the caller is not handed a shifted reply. */
                pio_sm_clear_fifos(bus->pio, bus->sm_rx);
                return HALF_DUPLEX_UART_ERR_TIMEOUT;
            }
            tight_loop_contents();
        }
        (void)rx_byte(bus);
    }

    return HALF_DUPLEX_UART_OK;
}

half_duplex_uart_result_t half_duplex_uart_transfer(half_duplex_uart_t *bus,
                                                    const uint8_t *tx, size_t tx_len,
                                                    uint8_t *rx, size_t rx_len,
                                                    uint32_t timeout_us)
{
    const half_duplex_uart_result_t sent = half_duplex_uart_write(bus, tx, tx_len);
    if (sent != HALF_DUPLEX_UART_OK) {
        return sent;
    }
    return half_duplex_uart_read_exact(bus, rx, rx_len, timeout_us);
}
