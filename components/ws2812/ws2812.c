#include "ws2812.h"

#include "hardware/clocks.h"
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

    *strip = (ws2812_strip_t){
        .pio         = config->pio,
        .sm          = (uint)sm,
        .offset      = offset,
        .pin         = config->pin,
        .pixels      = config->pixels,
        .length      = config->length,
        .is_rgbw     = config->is_rgbw,
        .brightness  = 255,
        .initialised = true,
    };

    state_machine_configure(strip,
        config->frequency_hz != 0 ? config->frequency_hz : WS2812_DEFAULT_FREQUENCY_HZ);

    ws2812_clear(strip);
    return WS2812_OK;
}

void ws2812_deinit(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return;
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

void ws2812_show(ws2812_strip_t *strip)
{
    if (strip == NULL || !strip->initialised) {
        return;
    }

    const uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + strip->sm);

    for (uint16_t i = 0; i < strip->length; i++) {
        const ws2812_color_t color = strip->brightness == 255
            ? strip->pixels[i]
            : ws2812_color_scale(strip->pixels[i], strip->brightness);
        pio_sm_put_blocking(strip->pio, strip->sm, ws2812_color_to_wire(color, strip->is_rgbw));
    }

    /*
     * put_blocking only guarantees the FIFO accepted the word, so wait for the
     * state machine to run dry before timing the latch gap. Clearing the sticky
     * flag first means an already-idle machine still re-asserts it, because it
     * keeps retrying the pull it is blocked on.
     */
    strip->pio->fdebug = stall_mask;
    while ((strip->pio->fdebug & stall_mask) == 0) {
        tight_loop_contents();
    }

    sleep_us(WS2812_RESET_US);
}
