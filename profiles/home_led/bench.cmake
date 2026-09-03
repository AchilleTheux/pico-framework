# home_led: a short strip on a desk.
#
# Eight LEDs draw little enough to run from the board's own 5 V pin, so the
# effects, the console and the Home Assistant integration can all be exercised
# without a separate supply. Everything else is identical to `default`.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(APP_LED_PIN   6 CACHE STRING "WS2812 data GPIO pin")
set(APP_LED_COUNT 8 CACHE STRING "Number of LEDs on the strip")
