# home_led - a WS2812 strip as a Home Assistant light.
#
# Needs a board with a radio (pico_w / pico2_w) to be useful. It still builds
# for one without: mqtt and wifi report MQTT_ERR_UNSUPPORTED, the console and
# the strip work, and CI keeps that path compiled.

add_executable(app_home_led
    main.c        # startup and the loop
    app.h         # the shared context every module below is handed

    light.c       # the model      -- host-tested, no SDK
    effects.c     # the frames     -- host-tested, no SDK
    ha.c          # the schema     -- host-tested, no SDK

    settings.c    # flash, and the commands that fill it
    net.c         # wifi, mqtt and the Home Assistant announcement
    render.c      # frames onto the strip, and the onboard LED
    console.c     # the command table and the interpreter
)

# ------------------------------------------------------------------------------
# Strip geometry
#
# A property of the installation, not of the firmware, so it comes from the
# profile. APP_LED_PIN / APP_LED_COUNT follow the convention simple_robot
# already uses.
# ------------------------------------------------------------------------------
set(APP_LED_PIN   6   CACHE STRING "WS2812 data GPIO pin")
set(APP_LED_COUNT 300 CACHE STRING "Number of LEDs on the strip")

# Which order the strip wants its colour bytes in: GRB for WS2812/WS2812B,
# RGB for many WS2815 and clones. If red comes out green, this is the setting.
# It can also be changed live with the console's `order` command.
set(APP_LED_ORDER "GRB" CACHE STRING "Wire colour order: GRB RGB BRG RBG GBR BGR")

target_compile_definitions(app_home_led PRIVATE
    APP_LED_PIN=${APP_LED_PIN}
    APP_LED_COUNT=${APP_LED_COUNT}
    APP_LED_ORDER="${APP_LED_ORDER}"
)

target_link_libraries(app_home_led PRIVATE
    pico_stdlib
    pico_framework::cli               # console over USB, with history
    pico_framework::firmware_update   # reflashing over that same console
    pico_framework::json              # the Home Assistant payloads
    pico_framework::mqtt              # the broker session
    pico_framework::persistent_config # credentials and the light's own state
    pico_framework::wifi              # the link underneath mqtt
    pico_framework::ws2812            # the strip
)

pico_enable_stdio_usb(app_home_led 1)
pico_enable_stdio_uart(app_home_led 0)

# Settings and the light's state are written to flash at runtime, so the linked
# image has to stay inside the application region.
pico_framework_reserve_flash_layout(app_home_led)

pico_framework_set_warnings(main.c light.c effects.c ha.c settings.c net.c render.c
                            console.c)

pico_add_extra_outputs(app_home_led)
