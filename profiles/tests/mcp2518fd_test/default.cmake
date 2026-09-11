# Classic 500 kbit/s; interoperates with can_test and mcp2515_test.
# External module with a 40 MHz crystal; override for your actual hardware.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(MCP2518FD_TEST_MODE "normal" CACHE STRING "Operating mode")
set(MCP2518FD_TEST_FD OFF CACHE BOOL "Enable CAN FD")
