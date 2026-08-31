# Drives the strip with DMA rather than by pushing the FIFO, and adds two
# checks that only make sense there: how long each path holds the processor up,
# and an animation driven without ever blocking.
#
# Costs four bytes a pixel for the wire buffer.

set(CMAKE_BUILD_TYPE    "Release" CACHE STRING "Build type")
set(WS2812_TEST_PIN     10        CACHE STRING "GPIO driving the strip's data line")
set(WS2812_TEST_LENGTH  60        CACHE STRING "Number of LEDs on the strip")
set(WS2812_TEST_IS_RGBW OFF       CACHE BOOL   "Strip has a dedicated white LED")
set(WS2812_TEST_USE_DMA ON        CACHE BOOL   "Exercise the DMA path")
