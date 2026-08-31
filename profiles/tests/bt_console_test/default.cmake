# Bluetooth alone, so this component brings the cyw43 arch. Combining it with
# the wifi component needs PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI=ON, since only
# one cyw43_arch may be linked.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI OFF CACHE BOOL "wifi provides the cyw43 arch")
