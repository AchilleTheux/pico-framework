# home_led: the installed strip.
#
# 300 LEDs on GPIO 6, which is what the firmware this was ported from drove.
# A strip this long at any real brightness needs its own 5 V supply with its
# ground tied to the board's.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(APP_LED_PIN   6   CACHE STRING "WS2812 data GPIO pin")
set(APP_LED_COUNT 300 CACHE STRING "Number of LEDs on the strip")

# This strip is a WS2815 that wants red first, confirmed on hardware: with the
# GRB default, `rgb 255 0 0` lit it green and `rgb 0 255 0` lit it red.
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
