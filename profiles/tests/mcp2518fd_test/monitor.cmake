# Silent CAN FD monitor; other active nodes must provide acknowledgements.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(MCP2518FD_TEST_MODE "listen_only" CACHE STRING "Operating mode")
set(MCP2518FD_TEST_FD ON CACHE BOOL "Enable CAN FD")
