# Default bench setup for the ws2812 hardware test: a short 16-LED RGB strip
# on GPIO 10.

set(CMAKE_BUILD_TYPE   "Release" CACHE STRING "Build type")
set(WS2812_TEST_PIN    10        CACHE STRING "GPIO driving the strip's data line")
set(WS2812_TEST_LENGTH 16        CACHE STRING "Number of LEDs on the strip")
set(WS2812_TEST_IS_RGBW OFF      CACHE BOOL   "Strip has a dedicated white LED")
