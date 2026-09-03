#include "render.h"

#if WIFI_SUPPORTED
#include "pico/cyw43_arch.h"
#endif

/* Slowly once there is a broker session, quickly while there is not. */
#define HEARTBEAT_LINKED_MS 1000u
#define HEARTBEAT_SEARCHING_MS 150u

void render_init(app_t *app)
{
    ws2812_order_t order;

    /*
     * The build's choice, unless it names something that is not an order at
     * all -- in which case the common one is a better guess than refusing to
     * light. The console's `order` command can change it live.
     */
    if (!ws2812_order_from_name(APP_LED_ORDER, &order)) {
        order = WS2812_ORDER_GRB;
    }

    const ws2812_config_t config = {
        .pio = pio0,
        .pin = APP_LED_PIN,
        .pixels = app->pixels,
        .length = APP_LED_COUNT,
        .is_rgbw = false,
        .order = order,
        .frequency_hz = WS2812_DEFAULT_FREQUENCY_HZ,

        /* A wire buffer is what enables DMA, and at 300 pixels the difference
           is 9 ms of the processor's time per frame -- which is most of the
           budget lwIP has to work in. */
        .wire_buffer = app->wire,
    };

    if (ws2812_init(&app->strip, &config) != WS2812_OK) {
        return;
    }

    /*
     * Gamma at the driver, brightness at the effects.
     *
     * Gamma has to be the last thing applied, after any scaling, or it is
     * being corrected and then re-linearised. Brightness stays at 255 here
     * because the effects apply it themselves with dithering, which is a
     * scaling decision and has to be made per pixel.
     */
    ws2812_set_brightness(&app->strip, 255);
    ws2812_set_gamma(&app->strip, ws2812_gamma_table);
    app->strip_ready = true;
}

void render_frame(app_t *app, uint32_t now_ms)
{
    if (!app->strip_ready || ws2812_is_busy(&app->strip)) {
        return;
    }

    if (app->show_test_pattern) {
        effects_render_test_pattern(app->pixels, APP_LED_COUNT);
    } else {
        effects_render(&app->effects, &app->light, app->pixels, APP_LED_COUNT, now_ms);
    }

    (void)ws2812_show_async(&app->strip);
}

void render_heartbeat(app_t *app, uint32_t now_ms)
{
#if WIFI_SUPPORTED
    static uint32_t last_ms;
    static bool lit;

    /* The LED hangs off the CYW43, so it is only reachable once wifi_init()
       has brought the chip up. Poking it after a failed init would be a call
       into a driver that was never started. */
    if (!app->radio_ready) {
        return;
    }

    const uint32_t interval =
        mqtt_is_connected(&app->mqtt) ? HEARTBEAT_LINKED_MS : HEARTBEAT_SEARCHING_MS;

    if (now_ms - last_ms < interval) {
        return;
    }
    last_ms = now_ms;
    lit = !lit;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, lit);
#else
    (void)app;
    (void)now_ms;
#endif
}
