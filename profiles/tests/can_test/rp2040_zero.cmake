# ==============================================================================
# can_test Profile: rp2040_zero.cmake
# ==============================================================================
# Active 500 kbit/s PIO node on a Waveshare RP2040-Zero with a 3.3 V CAN
# transceiver wired to the board's bottom-edge pads: the transceiver's RXD
# output on GPIO 29 and its TXD input on GPIO 28. Neither pin has another
# function on this board (see boards/rp2040_zero.h and the SDK's
# waveshare_rp2040_zero.h), so nothing else needs moving out of the way.
#
# Pairs with `make BOARD=rp2350_can APP=tests/mcp2515_test PROFILE=default`
# on the same bus: two nodes at the same bitrate, each acknowledging the
# other.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     0         CACHE STRING "PIO block")
set(CAN_TEST_RX_PIN  29        CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  28        CACHE STRING "Transceiver TXD pin")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
