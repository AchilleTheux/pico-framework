# The single onboard RGB LED on a Waveshare RP2040-Zero.

set(CMAKE_BUILD_TYPE   "Release" CACHE STRING "Build type")
set(WS2812_TEST_PIN    16        CACHE STRING "Onboard WS2812 data pin")
set(WS2812_TEST_LENGTH 1         CACHE STRING "One onboard RGB LED")
set(WS2812_TEST_IS_RGBW OFF      CACHE BOOL   "The onboard LED is RGB")
