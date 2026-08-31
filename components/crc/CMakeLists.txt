# Cyclic redundancy checks.
#
# Pure integer work with no SDK dependency, so the host tests compile crc.c
# directly and the whole component is verified without hardware.

add_library(pico_framework_crc STATIC
    crc.c
)

add_library(pico_framework::crc ALIAS pico_framework_crc)

target_include_directories(pico_framework_crc PUBLIC
    include
)

pico_framework_set_warnings(crc.c)
