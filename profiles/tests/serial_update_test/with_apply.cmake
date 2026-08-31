# Builds the fwapply command, which overwrites the running firmware.
#
# Read apps/tests/serial_update_test/README.md and
# components/firmware_update/include/firmware_apply.h before using this. There
# is a window during the install in which the board holds neither a complete
# old firmware nor a complete new one; losing power there leaves nothing to
# boot, and BOOTSEL over USB is the only way back.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(FIRMWARE_SERVICE_ENABLE_APPLY ON CACHE BOOL "Build the fwapply command")
set(SERIAL_UPDATE_BUILD_STAMP "A" CACHE STRING "Build stamp")
