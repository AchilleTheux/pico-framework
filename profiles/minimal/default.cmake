# Default profile for the minimal application.
#
# Loaded as an initial CMake cache with `cmake -C` when a build directory is
# first configured. A profile sets application settings only: it does not
# select the board or the application, and it does not list component
# dependencies.
#
# The defaults below match the application's own defaults; they are written
# out explicitly so this file documents the knobs a profile may set.

set(CMAKE_BUILD_TYPE      "Release" CACHE STRING "Build type")
set(MINIMAL_STDIO_USB     ON        CACHE BOOL   "Route stdio to USB CDC")
set(MINIMAL_STDIO_UART    ON        CACHE BOOL   "Route stdio to the board's default UART")
