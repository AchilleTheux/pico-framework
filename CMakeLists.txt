cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# pico-framework top level build (standalone mode)
#
# This file owns project(), Pico SDK initialization and the selection of the
# single application to build. See DESIGN_DOC.md sections 10 to 12.
#
# A build is identified by three inputs:
#
#   PICO_BOARD           hardware          (Pico SDK board mechanism)
#   PICO_FRAMEWORK_APP   firmware program  (a directory under apps/)
#   profile              settings          (cmake -C profiles/<app>/<profile>.cmake)
#
# Components are ordinary CMake dependencies, not a fourth selection knob.
# ---------------------------------------------------------------------------

set(PICO_FRAMEWORK_ROOT "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND CMAKE_MODULE_PATH "${PICO_FRAMEWORK_ROOT}/cmake")

include(project_options)

# Use the SDK bundled as a submodule rather than a global installation.
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${PICO_FRAMEWORK_ROOT}/lib/pico-sdk")
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR
        "Pico SDK not found at ${PICO_SDK_PATH}.\n"
        "Run: git submodule update --init --recursive")
endif()

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(pico_framework C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

pico_sdk_init()

# ---------------------------------------------------------------------------
# Components
#
# Every component directory is registered unconditionally with
# EXCLUDE_FROM_ALL, so declaring a component costs nothing until an
# application links it. Transitive dependencies between components are
# resolved by target_link_libraries().
# ---------------------------------------------------------------------------

include(components)
pico_framework_add_components()

# ---------------------------------------------------------------------------
# Application
#
# Exactly one application is built per build directory.
# ---------------------------------------------------------------------------

if(NOT EXISTS "${PICO_FRAMEWORK_ROOT}/apps/${PICO_FRAMEWORK_APP}/CMakeLists.txt")
    message(FATAL_ERROR
        "Unknown application '${PICO_FRAMEWORK_APP}': "
        "apps/${PICO_FRAMEWORK_APP}/CMakeLists.txt does not exist.")
endif()

add_subdirectory("apps/${PICO_FRAMEWORK_APP}")

pico_framework_report_configuration()
