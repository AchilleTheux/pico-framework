# ==============================================================================
# can_test Profile: pico2_w.cmake
# ==============================================================================
# Active 500 kbit/s PIO node on a Pico 2 W, with a 3.3 V CAN transceiver on
# the transceiver's RXD output at GPIO 26 and its TXD input at GPIO 27.
# Neither pin is one of the four the CYW43 radio takes (23, 24, 25, 29), so
# this coexists with a WiFi build even though can_test itself brings no radio
# up.
#
# This is the RP2350 counterpart of the rp2040_zero profile, and it exists
# because can2040 is not the same code on the two chips: its bit stuffer and
# unstuffer both dispatch to separate rp2040 and rp2350 implementations, it
# writes the rp2350-only PIO `gpiobase` register, and components/can's own
# pin-window check is compiled only when NUM_BANK0_GPIOS > 32. An RP2040
# result says nothing about any of that.
#
# Pairs with `make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=default`
# on the same bus.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     0         CACHE STRING "PIO block")
set(CAN_TEST_RX_PIN  26        CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  27        CACHE STRING "Transceiver TXD pin")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
