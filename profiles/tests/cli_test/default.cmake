# CLI on stdio: whichever of USB CDC and the default UART the build enables.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(CLI_TEST_USE_UART OFF      CACHE BOOL   "Run the CLI on a dedicated UART instead of stdio")
set(CLI_TEST_ECHO     ON       CACHE BOOL   "Echo typed characters back to the terminal")
