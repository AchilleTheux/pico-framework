# Erasing, programming and reading a bounded region of flash.
#
# flash_layout.c is pure arithmetic — the division of the chip and every bound
# check — so the host tests compile it directly. flash_storage.c is the part
# that touches the hardware.

add_library(pico_framework_flash_storage STATIC
    flash_layout.c
    flash_storage.c
)

add_library(pico_framework::flash_storage ALIAS pico_framework_flash_storage)

target_include_directories(pico_framework_flash_storage PUBLIC
    include
)

pico_framework_set_warnings(flash_layout.c flash_storage.c)

target_link_libraries(pico_framework_flash_storage PUBLIC
    pico_stdlib
    hardware_flash
    pico_flash
    pico_framework::crc
)

# ---------------------------------------------------------------------------
# The layout, at configure time
#
# flash_layout.h divides the chip; the division depends on the chip's size and
# on how many sectors are reserved for data. Both are decided here rather than
# in C, for two reasons that have nothing to do with taste:
#
#   * flash_layout.c includes no SDK header, so that the host tests can compile
#     it. PICO_FLASH_SIZE_BYTES lives in the board header, and therefore never
#     reaches it — left alone, every board would silently get the file's
#     2 MiB fallback layout, which on a 4 MiB part puts the staging region in
#     the middle of the chip and wastes the top half.
#
#   * the linker has to know where the application region ends, and a linker
#     script cannot read a C header.
#
# So the numbers are computed once, here, and pushed into both.
# ---------------------------------------------------------------------------

set(PICO_FRAMEWORK_FLASH_DATA_SECTORS 32 CACHE STRING
    "Sectors reserved at the end of flash for the manifest, config and logs")

# PICO_FLASH_SIZE_BYTES arrives as a linker-friendly expression, e.g.
# "(4 * 1024 * 1024)". math() evaluates it; the result is what both the
# compile definition and the linker script need.
if(PICO_FLASH_SIZE_BYTES)
    math(EXPR PICO_FRAMEWORK_FLASH_BYTES "${PICO_FLASH_SIZE_BYTES}")
else()
    math(EXPR PICO_FRAMEWORK_FLASH_BYTES "2 * 1024 * 1024")
endif()

# The same arithmetic as flash_layout_compute(); flash_layout.c asserts at
# compile time that the two agree, so this cannot drift unnoticed.
math(EXPR PICO_FRAMEWORK_FLASH_TOTAL_SECTORS "${PICO_FRAMEWORK_FLASH_BYTES} / 4096")
math(EXPR PICO_FRAMEWORK_FLASH_IMAGE_SECTORS
     "(${PICO_FRAMEWORK_FLASH_TOTAL_SECTORS} - ${PICO_FRAMEWORK_FLASH_DATA_SECTORS}) / 2")
math(EXPR PICO_FRAMEWORK_FLASH_APPLICATION_BYTES
     "${PICO_FRAMEWORK_FLASH_IMAGE_SECTORS} * 4096")

if(PICO_FRAMEWORK_FLASH_IMAGE_SECTORS LESS 1)
    message(FATAL_ERROR
        "flash_storage: ${PICO_FRAMEWORK_FLASH_BYTES} bytes of flash cannot hold "
        "${PICO_FRAMEWORK_FLASH_DATA_SECTORS} reserved sectors plus two images.")
endif()

# Deliberately not named PICO_FLASH_SIZE_BYTES: the board header defines that
# one as an expression, and a -D of the evaluated number would be a
# redefinition in every translation unit that sees both.
target_compile_definitions(pico_framework_flash_storage PUBLIC
    FLASH_LAYOUT_FLASH_SIZE=${PICO_FRAMEWORK_FLASH_BYTES}u
    FLASH_LAYOUT_DATA_SECTORS=${PICO_FRAMEWORK_FLASH_DATA_SECTORS}u
)

# The application region size as computed here, for flash_layout.c to hold its
# own arithmetic against with a _Static_assert. Not merged into the block above
# because it is a cross-check, not an input.
target_compile_definitions(pico_framework_flash_storage PRIVATE
    FLASH_LAYOUT_CMAKE_APPLICATION_SIZE=${PICO_FRAMEWORK_FLASH_APPLICATION_BYTES}u
)

# add_subdirectory() gives this file its own variable scope, and applications
# live in sibling scopes that never see it. The function below is called from
# one of those, so the number has to be somewhere global to reach it.
set_property(GLOBAL PROPERTY PICO_FRAMEWORK_FLASH_APPLICATION_BYTES
             "${PICO_FRAMEWORK_FLASH_APPLICATION_BYTES}")

# ---------------------------------------------------------------------------
# pico_framework_reserve_flash_layout(<target>)
#
# Stop the linker placing <target> outside the application region.
#
# Without this, the only thing standing between an oversized image and the
# staging region is flash_storage_holds_running_code(), which tests the address
# of a single function. That answers "is this region the one I am executing
# from", not "does my image end before this region begins" — a firmware linked
# from offset 0 that grows past the halfway point satisfies the check and still
# overlaps staging, so `fwbegin` would erase live code or read-only data.
#
# The fix is to make the region the linker's, not a convention: the FLASH
# memory region is redefined as the application region, and an image that no
# longer fits fails to link with "region FLASH overflowed by N bytes" instead
# of running until the first update attempt. The chip size the C code sees is
# unchanged — staging, manifest and data are still addressable, they are just
# no longer somewhere the image can be placed.
#
# Call it from an application that stages firmware updates or otherwise writes
# to flash regions other than its own. An application that never does may skip
# it and use the whole chip.
# ---------------------------------------------------------------------------
function(pico_framework_reserve_flash_layout target)
    get_property(application_bytes GLOBAL PROPERTY
                 PICO_FRAMEWORK_FLASH_APPLICATION_BYTES)
    if(NOT application_bytes)
        message(FATAL_ERROR
            "pico_framework_reserve_flash_layout(${target}): the flash_storage "
            "component has not been registered, so there is no layout to reserve.")
    endif()

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}-flash-layout")
    file(MAKE_DIRECTORY "${generated}")

    # Overrides the SDK's own pico_flash_region.ld, which memmap_default.ld
    # includes to declare the FLASH region. The override path is searched
    # first, so this file wins without patching the SDK.
    file(WRITE "${generated}/pico_flash_region.ld"
         "/* Generated by pico_framework_reserve_flash_layout(${target}).\n"
         " * LENGTH is the flash_layout.h application region, not the chip:\n"
         " * the rest of the chip holds staging, the manifest and data.\n"
         " */\n"
         "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${application_bytes}\n")

    pico_add_linker_script_override_path(${target} "${generated}"
                                         FILES pico_flash_region.ld)
endfunction()
