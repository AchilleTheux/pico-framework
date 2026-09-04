# ==============================================================================
# can_test Profile: pio2.cmake
# ==============================================================================
# The pico2_w wiring driven from PIO block 2 instead of block 0.
#
# That third block exists only on RP2350, and reaching it exercises code no
# RP2040 build even compiles: can2040's RESETS_RESET_PIO2_BITS branch and its
# pio2_hw selection, components/can's `case 2` IRQ handler, and the
# PIO_IRQ_NUM(pio2, 0) the component installs it on. Every other profile here
# uses block 0, so without this one none of that is covered.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     2         CACHE STRING "PIO block: RP2350's third one")
set(CAN_TEST_RX_PIN  26        CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  27        CACHE STRING "Transceiver TXD pin")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
