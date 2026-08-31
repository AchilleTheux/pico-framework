# Passive 500 kbit/s monitor. The transceiver TXD input must be held high.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     0         CACHE STRING "PIO block")
set(CAN_TEST_RX_PIN  4         CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  -1        CACHE STRING "No TX pin: silent monitor")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
