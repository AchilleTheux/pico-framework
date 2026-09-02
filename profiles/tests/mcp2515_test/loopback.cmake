# ==============================================================================
# mcp2515_test Profile: loopback.cmake
# ==============================================================================
# Self-test: the controller ACKs and receives its own transmissions
# internally. Validates the SPI wiring, bit timing, and this driver on a
# single board — no second CAN node, no transceiver, not even CANH/CANL
# needs to be connected.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(MCP2515_TEST_MODE "loopback" CACHE STRING "normal, loopback, or listen_only")
