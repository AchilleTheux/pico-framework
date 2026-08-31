# Active 500 kbit/s CAN node using PIO0 and the can2040 example pinout.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     0         CACHE STRING "PIO block")
set(CAN_TEST_RX_PIN  4         CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  5         CACHE STRING "Transceiver TXD pin")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
