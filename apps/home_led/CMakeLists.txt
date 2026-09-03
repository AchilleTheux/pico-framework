# home_led - a WS2812 strip as a Home Assistant light.
#
# Needs a board with a radio (pico_w / pico2_w) to be useful. It still builds
# for one without: mqtt and wifi report MQTT_ERR_UNSUPPORTED, the console and
# the strip work, and CI keeps that path compiled.

add_executable(app_home_led
    main.c
    light.c
    effects.c
    ha.c
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

target_compile_definitions(app_home_led PRIVATE
    APP_LED_PIN=${APP_LED_PIN}
    APP_LED_COUNT=${APP_LED_COUNT}
)

target_link_libraries(app_home_led PRIVATE
    pico_stdlib
    pico_framework::cli               # console over USB
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

pico_framework_set_warnings(main.c light.c effects.c ha.c)

pico_add_extra_outputs(app_home_led)
