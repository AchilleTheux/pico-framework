#include "ws2812.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pico/time.h"

#include "ws2812.pio.h"

/*
 * Strips on the same PIO block share one copy of the program. Slot i tracks
 * PIO block i; a stored value of zero means "not loaded", so the offset is
 * kept biased by one and a program loaded at offset 0 is still representable.
 */
static struct {
    uint16_t offset_plus_one;
    uint16_t users;
} g_program[NUM_PIOS];

static bool program_acquire(PIO pio, uint *offset_out)
{
    const uint index = pio_get_index(pio);

    if (g_program[index].offset_plus_one == 0) {
        const int offset = pio_add_program(pio, &ws2812_program);
        if (offset < 0) {
            return false;
        }
        g_program[index].offset_plus_one = (uint16_t)(offset + 1);
    }

    g_program[index].users++;
    *offset_out = (uint)(g_program[index].offset_plus_one - 1u);
    return true;
}

static void program_release(PIO pio)
{
    const uint index = pio_get_index(pio);

    if (g_program[index].users == 0) {
        return;
    }

    if (--g_program[index].users == 0) {
        pio_remove_program(pio, &ws2812_program,
                           (uint)(g_program[index].offset_plus_one - 1u));
        g_program[index].offset_plus_one = 0;
    }
}

static void state_machine_configure(const ws2812_strip_t *strip, uint32_t frequency_hz)
{
    pio_gpio_init(strip->pio, strip->pin);
    pio_sm_set_consecutive_pindirs(strip->pio, strip->sm, strip->pin, 1, true);

    pio_sm_config c = ws2812_program_get_default_config(strip->offset);
    sm_config_set_sideset_pins(&c, strip->pin);

    /* Shift left so the MSB goes first, and autopull once the pixel's worth of
       bits has been consumed. The threshold is what makes ws2812_color_to_wire's
       left-aligned layout the right one. */
    sm_config_set_out_shift(&c, false, true, strip->is_rgbw ? 32 : 24);

    /* The RX half is unused; give its FIFO entries to TX to reduce stalling. */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    const int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const float div = (float)clock_get_hz(clk_sys) / ((float)frequency_hz * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(strip->pio, strip->sm, strip->offset, &c);
    pio_sm_set_enabled(strip->pio, strip->sm, true);
}

/*
 * Set up the channel that feeds the state machine.
 *
 * pio_get_dreq() rather than an arithmetic guess: the data request number for a
 * TX FIFO depends on which PIO block and which state machine, and the mapping
 * differs between RP2040's two blocks and RP2350's three. Working it out by
 * hand is how a strip ends up driven by the wrong FIFO's pacing.
 */
static void dma_configure(ws2812_strip_t *strip)
{
    dma_channel_config c = dma_channel_get_default_config((uint)strip->dma_channel);

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(strip->pio, strip->sm, true));

    dma_channel_configure((uint)strip->dma_channel, &c,
                          &strip->pio->txf[strip->sm],  /* always the same word */
                          strip->wire_buffer,
                          strip->length,
                          false);                        /* started per frame */
}

ws2812_result_t ws2812_init(ws2812_strip_t *strip, const ws2812_config_t *config)
{
    if (strip == NULL || config == NULL || config->pio == NULL ||
        config->pixels == NULL || config->length == 0) {
        return WS2812_ERR_INVALID_ARG;
    }

    uint offset;
    if (!program_acquire(config->pio, &offset)) {
        return WS2812_ERR_NO_PROGRAM_SPACE;
    }

    const int sm = pio_claim_unused_sm(config->pio, false);
    if (sm < 0) {
        program_release(config->pio);
        return WS2812_ERR_NO_STATE_MACHINE;
    }

    int dma_channel = -1;
    if (config->wire_buffer != NULL) {
        dma_channel = dma_claim_unused_channel(false);
        if (dma_channel < 0) {
            pio_sm_unclaim(config->pio, (uint)sm);
            program_release(config->pio);
            return WS2812_ERR_NO_DMA_CHANNEL;
        }
    }

    *strip = (ws2812_strip_t){
        .pio         = config->pio,
        .sm          = (uint)sm,
        .offset      = offset,
        .pin         = config->pin,
        .pixels      = config->pixels,
        .length      = config->length,
        .is_rgbw     = config->is_rgbw,
        .order       = config->order,
        .brightness  = 255,
        .gamma_table = NULL,
        .wire_buffer = config->wire_buffer,
        .dma_channel = dma_channel,
        .initialised = true,
    };

    state_machine_configure(strip,
        config->frequency_hz != 0 ? config->frequency_hz : WS2812_DEFAULT_FREQUENCY_HZ);

    if (strip->dma_channel >= 0) {
        dma_configure(strip);
    }

    ws2812_clear(strip);
    return WS2812_OK;
}

void ws2812_deinit(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }

    if (strip->dma_channel >= 0) {
        /* Abort rather than wait: deinit may well be being called because
           something has gone wrong. */
        dma_channel_abort((uint)strip->dma_channel);
        dma_channel_unclaim((uint)strip->dma_channel);
        strip->dma_channel = -1;
    }

    pio_sm_set_enabled(strip->pio, strip->sm, false);
    pio_sm_unclaim(strip->pio, strip->sm);
    program_release(strip->pio);

    strip->initialised = false;
}

void ws2812_set_pixel(ws2812_strip_t *strip, uint16_t index, ws2812_color_t color)
{
    if (strip == NULL || !strip->initialised || index >= strip->length) {
        return;
    }
    strip->pixels[index] = color;
}

ws2812_color_t ws2812_get_pixel(const ws2812_strip_t *strip, uint16_t index)
{
    if (strip == NULL || !strip->initialised || index >= strip->length) {
        return WS2812_COLOR_BLACK;
    }
    return strip->pixels[index];
}

void ws2812_fill(ws2812_strip_t *strip, ws2812_color_t color)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }
    for (uint16_t i = 0; i < strip->length; i++) {
        strip->pixels[i] = color;
    }
}

void ws2812_clear(ws2812_strip_t *strip)
{
    ws2812_fill(strip, WS2812_COLOR_BLACK);
}

void ws2812_set_brightness(ws2812_strip_t *strip, uint8_t brightness)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }
    strip->brightness = brightness;
}

uint8_t ws2812_get_brightness(const ws2812_strip_t *strip)
{
    return (strip != NULL && strip->initialised) ? strip->brightness : 0;
}

void ws2812_set_gamma(ws2812_strip_t *strip, const uint8_t *table)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }
    strip->gamma_table = table;
}

void ws2812_set_order(ws2812_strip_t *strip, ws2812_order_t order)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }
    strip->order = order;
}

ws2812_order_t ws2812_get_order(const ws2812_strip_t *strip)
{
    return (strip != NULL && strip->initialised) ? strip->order : WS2812_ORDER_GRB;
}

/* The sticky flag the state machine raises when it stalls on an empty FIFO,
   which is how "everything has been clocked out" is detected. */
static uint32_t stall_mask(const ws2812_strip_t *strip)
{
    return 1u << (PIO_FDEBUG_TXSTALL_LSB + strip->sm);
}

/*
 * One pixel, from what the application wrote to what the state machine wants.
 *
 * Brightness first, then gamma: gamma says what value produces a given
 * apparent brightness, so it has to be the last thing that touches the
 * number. Both paths below go through here so that ordering cannot end up
 * different in one of them.
 */
static uint32_t encode_pixel(const ws2812_strip_t *strip, uint16_t index)
{
    ws2812_color_t color = strip->pixels[index];

    if (strip->brightness != 255) {
        color = ws2812_color_scale(color, strip->brightness);
    }
    if (strip->gamma_table != NULL) {
        color = (ws2812_color_t){
            .r = strip->gamma_table[color.r],
            .g = strip->gamma_table[color.g],
            .b = strip->gamma_table[color.b],
            .w = strip->gamma_table[color.w],
        };
    }

    return ws2812_color_to_wire(color, strip->is_rgbw, strip->order);
}

/* Convert the pixel buffer into wire words for the DMA transfer. */
static void fill_wire_buffer(ws2812_strip_t *strip)
{
    for (uint16_t i = 0; i < strip->length; i++) {
        strip->wire_buffer[i] = encode_pixel(strip, i);
    }
}

bool ws2812_is_busy(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return false;
    }

    /* Still feeding the FIFO. */
    if (strip->dma_channel >= 0 && dma_channel_is_busy((uint)strip->dma_channel)) {
        return true;
    }

    /*
     * The FIFO may be drained but the state machine still clocking out the last
     * pixel. It raises the stall flag when it runs dry; until then the wire is
     * still busy.
     */
    if ((strip->pio->fdebug & stall_mask(strip)) == 0) {
        return true;
    }

    /*
     * Transmission is over. What remains is the strip's latch time, which is
     * counted from the moment the machine went quiet rather than from the start
     * of the frame — so the deadline is recorded the first time it is seen.
     */
    if (strip->latch_ready_us == 0) {
        strip->latch_ready_us = time_us_64() + WS2812_RESET_US;
    }

    return time_us_64() < strip->latch_ready_us;
}

void ws2812_wait(ws2812_strip_t *strip)
{
    while (ws2812_is_busy(strip)) {
        tight_loop_contents();
    }
}

ws2812_result_t ws2812_show_async(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return WS2812_ERR_INVALID_ARG;
    }
    if (strip->dma_channel < 0) {
        return WS2812_ERR_NO_WIRE_BUFFER;
    }
    if (ws2812_is_busy(strip)) {
        return WS2812_ERR_BUSY;
    }

    /*
     * Copied now, not read during the transfer, so the caller may go on
     * modifying the pixel buffer the moment this returns. That is the whole
     * reason the wire buffer is a second array rather than the pixel buffer
     * reinterpreted.
     */
    fill_wire_buffer(strip);

    /* Cleared before the transfer, so seeing it again means *this* frame has
       finished rather than the last one. */
    strip->pio->fdebug = stall_mask(strip);
    strip->latch_ready_us = 0;

    dma_channel_transfer_from_buffer_now((uint)strip->dma_channel,
                                         strip->wire_buffer, strip->length);
    return WS2812_OK;
}

void ws2812_show(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }

    if (strip->dma_channel >= 0) {
        /* Same path as the async one, then wait. Keeping one implementation of
           the transfer means the blocking and non-blocking cases cannot drift. */
        if (ws2812_show_async(strip) == WS2812_ERR_BUSY) {
            ws2812_wait(strip);
            (void)ws2812_show_async(strip);
        }
        ws2812_wait(strip);
        return;
    }

    /* No wire buffer: push straight into the FIFO, which needs no extra memory
       and is all a caller that never animates requires. */
    for (uint16_t i = 0; i < strip->length; i++) {
        pio_sm_put_blocking(strip->pio, strip->sm, encode_pixel(strip, i));
    }

    /*
     * put_blocking only guarantees the FIFO accepted the word, so wait for the
     * state machine to run dry before timing the latch gap. Clearing the sticky
     * flag first means an already-idle machine still re-asserts it, because it
     * keeps retrying the pull it is blocked on.
     */
    strip->pio->fdebug = stall_mask(strip);
    while ((strip->pio->fdebug & stall_mask(strip)) == 0) {
        tight_loop_contents();
    }

    sleep_us(WS2812_RESET_US);
}
