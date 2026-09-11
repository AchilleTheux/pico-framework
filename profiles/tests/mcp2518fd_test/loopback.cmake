# Internal loopback tests classic, RTR, FD and 64-byte FD+BRS messages.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(MCP2518FD_TEST_MODE "loopback" CACHE STRING "Operating mode")
set(MCP2518FD_TEST_FD ON CACHE BOOL "Enable CAN FD")
