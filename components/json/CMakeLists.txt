# Reading and writing small JSON documents.
#
# Pure string and integer work with no SDK dependency, so the host tests
# compile json.c directly and the whole component is verified without
# hardware.

add_library(pico_framework_json STATIC
    json.c
)

add_library(pico_framework::json ALIAS pico_framework_json)

target_include_directories(pico_framework_json PUBLIC
    include
)

pico_framework_set_warnings(json.c)
