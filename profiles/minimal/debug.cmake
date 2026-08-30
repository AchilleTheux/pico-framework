# Debug profile for the minimal application: unoptimized build, stdio on UART
# only so a USB re-enumeration cannot swallow early output.

set(CMAKE_BUILD_TYPE      "Debug" CACHE STRING "Build type")
set(MINIMAL_STDIO_USB     OFF      CACHE BOOL   "Route stdio to USB CDC")
set(MINIMAL_STDIO_UART    ON       CACHE BOOL   "Route stdio to the board's default UART")
