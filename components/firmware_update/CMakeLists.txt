# Firmware image format and the boot decision.
#
# firmware_image.c is pure: the image header, its checksums, and the rule for
# choosing what to boot. The host tests compile it directly, so the part of a
# bootloader that is most expensive to get wrong is verified without hardware.
#
# The flash-backed half — staging a received image and installing it — comes
# later and will live beside it in this component.

add_library(pico_framework_firmware_update STATIC
    firmware_image.c
)

add_library(pico_framework::firmware_update ALIAS pico_framework_firmware_update)

target_include_directories(pico_framework_firmware_update PUBLIC
    include
)

pico_framework_set_warnings(firmware_image.c)

target_link_libraries(pico_framework_firmware_update PUBLIC
    pico_framework::crc
)
