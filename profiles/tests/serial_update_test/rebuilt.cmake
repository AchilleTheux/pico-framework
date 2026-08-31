# The same firmware with a different build stamp, to send as an update: flash
# 'with_apply', then send the image this profile produces and check that the
# version command reports B instead of A.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(FIRMWARE_SERVICE_ENABLE_APPLY ON CACHE BOOL "Build the fwapply command")
set(SERIAL_UPDATE_BUILD_STAMP "B" CACHE STRING "Build stamp")
