# ==============================================================================
# mcp2515_test Profile: monitor.cmake
# ==============================================================================
# Silent monitor: receive-only, transmits and ACKs nothing. See can_test's
# equivalent profile for the same idea on the PIO backend.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(MCP2515_TEST_MODE "listen_only" CACHE STRING "normal, loopback, or listen_only")
