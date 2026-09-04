# ==============================================================================
# can_test Profile: filter.cmake
# ==============================================================================
# The rp2040_zero wiring, plus one software acceptance filter, so that a
# filter which is quietly accepting everything can be told apart from one
# that works. This application sends a standard 0x123 frame, an extended
# 0x01ABCDE0 frame, and a 0x321 RTR frame in rotation, so a peer running it
# should be seen here as 0x123 frames only — one received line per three the
# peer sends, with `filtered` rising by two for each one accepted.
#
# Set CAN_TEST_FILTER to ext_only for the complementary case (extended
# frames only, which selects on the flag rather than on the value).
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CAN_TEST_PIO     0         CACHE STRING "PIO block")
set(CAN_TEST_RX_PIN  29        CACHE STRING "Transceiver RXD pin")
set(CAN_TEST_TX_PIN  28        CACHE STRING "Transceiver TXD pin")
set(CAN_TEST_BITRATE 500000    CACHE STRING "CAN bus bitrate")
set(CAN_TEST_FILTER  "id_123"  CACHE STRING "Accept only standard id 0x123")
