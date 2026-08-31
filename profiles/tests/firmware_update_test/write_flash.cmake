# Also erases and programs a sector of the staging region, to check the flash
# path end to end. Destructive to staging; it never touches the running
# firmware, which the component refuses on principle.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(FIRMWARE_UPDATE_TEST_WRITE_FLASH ON CACHE BOOL "Also erase and program a staging sector")
