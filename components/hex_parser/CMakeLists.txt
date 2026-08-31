# Intel HEX record decoding.
#
# No SDK dependency: the host tests compile hex_parser.c directly, against real
# records taken from a linker's output.

add_library(pico_framework_hex_parser STATIC
    hex_parser.c
)

add_library(pico_framework::hex_parser ALIAS pico_framework_hex_parser)

target_include_directories(pico_framework_hex_parser PUBLIC
    include
)

pico_framework_set_warnings(hex_parser.c)
