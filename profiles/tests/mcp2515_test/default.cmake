# ==============================================================================
# mcp2515_test Profile: default.cmake
# ==============================================================================
# Normal mode: a real CAN bus with at least one other active node, wired
# through a real controller and a transceiver of its own. Pins default to
# the rp2350_can board's onboard XL2515 (BOARD=rp2350_can); override them for
# any other wiring.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(MCP2515_TEST_MODE "normal" CACHE STRING "normal, loopback, or listen_only")
