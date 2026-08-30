# CLI on stdio with echo off, for a program on the other end rather than a
# terminal: no echoed characters to strip out of the reply stream.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CLI_TEST_USE_UART OFF      CACHE BOOL   "Run the CLI on a dedicated UART instead of stdio")
set(CLI_TEST_ECHO     OFF      CACHE BOOL   "Echo typed characters back to the terminal")
