# Framework-wide build settings and the BOARD / APP / PROFILE selection model.
#
# Three inputs identify a build:
#   PICO_BOARD           - hardware, handled by the Pico SDK board mechanism
#   PICO_FRAMEWORK_APP   - the application under apps/ to build
#   a profile            - an initial CMake cache file loaded on first configure
#
# The profile is not a CMake variable the build reads; it is applied with
# `cmake -C profiles/${APP}/${PROFILE}.cmake` before the cache exists.
# PICO_FRAMEWORK_PROFILE is recorded here for reporting only.

set(PICO_FRAMEWORK_APP "minimal" CACHE STRING
    "Application under apps/ to build")

set(PICO_FRAMEWORK_PROFILE "default" CACHE STRING
    "Name of the profile this build directory was configured with (informational)")

# Custom board headers live beside the SDK's own, so PICO_BOARD may name either
# an SDK board or one of ours.
list(APPEND PICO_BOARD_HEADER_DIRS "${PICO_FRAMEWORK_ROOT}/boards")

function(pico_framework_report_configuration)
    message(STATUS "")
    message(STATUS "pico-framework configuration")
    message(STATUS "  BOARD    : ${PICO_BOARD}")
    message(STATUS "  APP      : ${PICO_FRAMEWORK_APP}")
    message(STATUS "  PROFILE  : ${PICO_FRAMEWORK_PROFILE}")
    message(STATUS "  PLATFORM : ${PICO_PLATFORM}")
    message(STATUS "  SDK      : ${PICO_SDK_VERSION_STRING} (${PICO_SDK_PATH})")
    message(STATUS "  BUILD    : ${CMAKE_BUILD_TYPE}")
    message(STATUS "")
endfunction()
