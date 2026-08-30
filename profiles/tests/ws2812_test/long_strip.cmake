# A 50-LED strip on GPIO 11. Long strips draw enough current to need their own
# supply; see the test's README.

set(CMAKE_BUILD_TYPE   "Release" CACHE STRING "Build type")
set(WS2812_TEST_PIN    11        CACHE STRING "GPIO driving the strip's data line")
set(WS2812_TEST_LENGTH 50        CACHE STRING "Number of LEDs on the strip")
set(WS2812_TEST_IS_RGBW OFF      CACHE BOOL   "Strip has a dedicated white LED")
