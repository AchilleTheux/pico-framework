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
    firmware_receive.c
    firmware_service.c
    firmware_apply.c
)

# Building the command that overwrites the running firmware is opt-in. Off, the
# component is safe to link into anything: it can receive and verify an image
# but has no way to install one.
option(FIRMWARE_SERVICE_ENABLE_APPLY
       "Build the fwapply command, which overwrites the running firmware" OFF)

add_library(pico_framework::firmware_update ALIAS pico_framework_firmware_update)

target_include_directories(pico_framework_firmware_update PUBLIC
    include
)

target_compile_definitions(pico_framework_firmware_update PUBLIC
    FIRMWARE_SERVICE_ENABLE_APPLY=$<BOOL:${FIRMWARE_SERVICE_ENABLE_APPLY}>
)

pico_framework_set_warnings(firmware_image.c firmware_receive.c
                            firmware_service.c firmware_apply.c)

target_link_libraries(pico_framework_firmware_update PUBLIC
    pico_framework::crc
    pico_framework::hex_parser
    pico_framework::cli
    pico_framework::flash_storage
    hardware_flash
    hardware_watchdog
)
