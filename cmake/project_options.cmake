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

# Warnings for the framework's own code.
#
# These are applied per source file, not per target, and that is deliberate.
# The Pico SDK ships most of its libraries as INTERFACE targets carrying
# sources, so linking pico_stdlib compiles SDK .c files *into* our target: a
# target-level flag would land on SDK code as well. The SDK is pinned and not
# ours to fix, and -Wundef in particular fails to build it outright.
#
# Call from a component or application CMakeLists with its own sources:
#
#   pico_framework_set_warnings(ws2812.c ws2812_color.c)
#
# Warnings are not errors by default: one must never stop someone debugging on
# hardware. Configure with -DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON in CI. The
# host tests are strict already, which is the cheap place for it.

option(PICO_FRAMEWORK_WARNINGS_AS_ERRORS
    "Treat warnings in framework and application source files as errors" OFF)

function(pico_framework_set_warnings)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()

    set(flags -Wall -Wextra -Wshadow -Wundef)
    if(PICO_FRAMEWORK_WARNINGS_AS_ERRORS)
        list(APPEND flags -Werror)
    endif()

    foreach(source IN LISTS ARGN)
        get_source_file_property(existing "${source}" COMPILE_OPTIONS)
        if(existing)
            list(APPEND flags ${existing})
        endif()
        set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "${flags}")
    endforeach()
endfunction()

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
