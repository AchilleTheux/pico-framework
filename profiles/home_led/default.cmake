# home_led: the installed strip.
#
# 300 LEDs on GPIO 6, which is what the firmware this was ported from drove.
# A strip this long at any real brightness needs its own 5 V supply with its
# ground tied to the board's.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(APP_LED_PIN   6   CACHE STRING "WS2812 data GPIO pin")
set(APP_LED_COUNT 300 CACHE STRING "Number of LEDs on the strip")
