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
