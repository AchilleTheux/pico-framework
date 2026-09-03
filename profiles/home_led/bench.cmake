# home_led: a short strip on a desk.
#
# Eight LEDs draw little enough to run from the board's own 5 V pin, so the
# effects, the console and the Home Assistant integration can all be exercised
# without a separate supply. Everything else is identical to `default`.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(APP_LED_PIN   6 CACHE STRING "WS2812 data GPIO pin")
set(APP_LED_COUNT 8 CACHE STRING "Number of LEDs on the strip")

# Same strip as `default`, just less of it.
set(APP_LED_ORDER "RGB" CACHE STRING "Wire colour order")

# ------------------------------------------------------------------------------
# Reflashing over the console
# ------------------------------------------------------------------------------
# `fwbegin` / `fwverify` are safe anywhere; `fwapply` overwrites the running
# image and reboots, which is the one step that can leave a board needing
# BOOTSEL. It is on here because a light in a fixed installation is exactly
# the case the serial updater exists for -- the console is reachable when the
# board is not.
#
#   make BOARD=pico2_w APP=home_led flash-serial PORT=/dev/ttyACM0 APPLY=1
set(FIRMWARE_SERVICE_ENABLE_APPLY ON CACHE BOOL
    "Allow the running firmware to install a staged image")
