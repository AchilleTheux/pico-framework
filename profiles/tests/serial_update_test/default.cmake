# Receive and verify only. The install step is not built, so this firmware can
# be flashed onto any board without risk: it can take an image and check it,
# but has no way to overwrite itself.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(FIRMWARE_SERVICE_ENABLE_APPLY OFF CACHE BOOL "Build the fwapply command")
set(SERIAL_UPDATE_BUILD_STAMP "A" CACHE STRING "Build stamp")
