/*
 * app - everything this firmware is holding on to, in one place.
 *
 * main.c used to keep all of this as file statics, which worked only for as
 * long as there was one file. Splitting the wiring across settings.c, net.c,
 * render.c and console.c means they need a way to reach the same state, and a
 * struct passed to each of them is the framework's own answer to that
 * (DESIGN_DOC.md section 13): the components here take caller-owned state
 * rather than keeping their own, and an application has no better excuse for
 * globals than a component does.
 *
 * It also makes the console honest. Every command already receives a
 * `user_data` pointer it was ignoring; now that pointer is the app, so a
 * command reaches the light through its argument rather than through a static
 * that happens to be in scope.
 */

#ifndef HOME_LED_APP_H
#define HOME_LED_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "pico/time.h"

#include "cli.h"
#include "effects.h"
#include "firmware_service.h"
#include "ha.h"
#include "light.h"
#include "mqtt.h"
#include "persistent_config.h"
#include "wifi.h"
#include "ws2812.h"

/* ---------------------------------------------------------------------------
 * Timing
 * -------------------------------------------------------------------------*/

/*
 * 50 frames a second. 300 pixels occupy the wire for about 9 ms, so this is
 * comfortably clear of the strip's own limit and leaves the rest of the loop
 * -- mostly lwIP -- the majority of every period.
 */
#define FRAME_INTERVAL_MS 20u

/*
 * How long the light must sit unchanged before its settings are written to
 * flash.
 *
 * Home Assistant sends a message per step while a slider is dragged. Writing
 * each one would be dozens of erase/program cycles for one gesture, so the
 * write waits until the gesture is over. Losing the last few seconds of
 * fiddling to a power cut is a much smaller problem than wearing the sector
 * out.
 */
#define SAVE_QUIET_MS 10000u

/* And a floor on how often the state is published, for the same reason
   applied to the broker rather than to flash. */
#define PUBLISH_INTERVAL_MS 250u

/* ---------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------*/

/*
 * The settings typed in over the console and kept in flash.
 *
 * Held as strings because that is what they are on the console and what
 * config_store keeps, and because wifi_config_t and mqtt_config_t borrow
 * rather than copy them -- so they have to live somewhere that outlives the
 * connection, which is here.
 */
typedef struct {
    char ssid[WIFI_SSID_MAX_LENGTH + 1];
    char password[WIFI_PASSWORD_MAX_LENGTH + 1];
    char hostname[33];
    char broker_host[128];
    char broker_port[8];
    char mqtt_user[64];
    char mqtt_pass[64];
    char device_id[HA_DEVICE_ID_MAX_LENGTH + 1];
    char ha_prefix[32];
} stored_settings_t;

typedef struct {
    /* The light, and the animation selected for it. */
    light_t light;
    effects_t effects;

    /* The strip, and the two buffers it needs. `pixels` is what effects.c
       writes; `wire` is what DMA reads, in the strip's own byte order. */
    ws2812_strip_t strip;
    ws2812_color_t pixels[APP_LED_COUNT];
    uint32_t wire[APP_LED_COUNT];
    bool strip_ready;
    bool show_test_pattern;

    /* The console, and the firmware updater sharing it. */
    cli_t cli;
    firmware_service_t firmware;
    bool firmware_ready;

    /* The network, and Home Assistant on top of it. */
    wifi_t wifi;
    mqtt_t mqtt;
    ha_t ha;
    bool radio_ready;
    bool ha_ready;

    /* Flash. */
    persistent_config_t config;
    uint8_t config_buffer[1024];
    stored_settings_t stored;

    /*
     * What the broker and the flash have each last been told, and when the
     * light last actually moved. light.generation is compared against these
     * to decide when to publish and when to write.
     */
    uint32_t published_generation;
    uint32_t saved_generation;
    uint32_t seen_generation;
    uint32_t last_change_ms;
    uint32_t last_publish_ms;
} app_t;

/*
 * Milliseconds since boot, which is what every module here takes as its idea
 * of time. Wraps a little under every 50 days; everything downstream uses
 * unsigned subtraction, so that is a non-event rather than a bug waiting for
 * a long-running light.
 */
static inline uint32_t app_now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

#endif /* HOME_LED_APP_H */
