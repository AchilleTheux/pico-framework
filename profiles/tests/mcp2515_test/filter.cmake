# ==============================================================================
# mcp2515_test Profile: filter.cmake
# ==============================================================================
# Normal mode with one hardware acceptance filter installed, the SPI-side
# counterpart of can_test's filter profile. A peer running either test
# application sends a standard 0x123 frame, an extended 0x01ABCDE0 frame,
# and a 0x321 RTR frame in rotation; only the 0x123 frames should appear
# here.
#
# This is also the check on the RXF0..RXF5 / RXM0..RXM1 bank split: the
# controller has two receive buffers with unequal filter banks and no way to
# disable either, so a plan that left one bank unconfigured would still
# deliver the extended and RTR frames.
# ==============================================================================

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

set(MCP2515_TEST_MODE   "normal" CACHE STRING "normal, loopback, or listen_only")
set(MCP2515_TEST_FILTER "id_123" CACHE STRING "Accept only standard id 0x123")
