cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Host-side unit tests.
#
# This is a separate CMake project built with the host compiler, not the ARM
# cross-toolchain: it covers the parts of components that are pure logic and
# call no Pico SDK function (DESIGN_DOC.md section 19). Anything needing real
# hardware is covered by a test application under apps/tests/ instead.
#
#   cmake -S tests -B build/host-tests
#   cmake --build build/host-tests
#   ctest --test-dir build/host-tests
#
# or simply `make test` from the repository root.
# ---------------------------------------------------------------------------

project(pico_framework_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(PICO_FRAMEWORK_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

enable_testing()

# Warnings are errors here: host tests are the cheapest place to catch a
# mistake, and nothing in this project's own sources should warn.
add_library(host_test_options INTERFACE)
target_include_directories(host_test_options INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/support"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(host_test_options INTERFACE
        -Wall -Wextra -Werror
    )
endif()

# Sanitizers when the compiler has them; they catch the overflow and
# out-of-bounds mistakes that integer-heavy component code invites.
option(HOST_TESTS_SANITIZE "Build host tests with ASan and UBSan" ON)
if(HOST_TESTS_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(host_test_options INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(host_test_options INTERFACE
        -fsanitize=address,undefined)
endif()

# host_test(<name> SOURCES <files...>)
#
# <name> names both the executable and the CTest test. Sources are relative to
# the repository root so a test can pull in the component file it covers.
function(host_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    set(resolved "")
    foreach(source IN LISTS ARG_SOURCES)
        if(IS_ABSOLUTE "${source}")
            list(APPEND resolved "${source}")
        else()
            list(APPEND resolved "${PICO_FRAMEWORK_ROOT}/${source}")
        endif()
    endforeach()

    add_executable(${name} ${resolved})
    target_link_libraries(${name} PRIVATE host_test_options)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

host_test(ws2812_color_test
    SOURCES
        tests/components/ws2812_color_test.c
        components/ws2812/ws2812_color.c
)
target_include_directories(ws2812_color_test PRIVATE
    "${PICO_FRAMEWORK_ROOT}/components/ws2812/include"
)

host_test(cli_test
    SOURCES
        tests/components/cli_test.c
        components/cli/cli.c
)
target_include_directories(cli_test PRIVATE
    "${PICO_FRAMEWORK_ROOT}/components/cli/include"
)
