# Byte FIFO over caller-owned storage.
#
# No SDK dependency, so the host tests compile ring_buffer.c directly.

add_library(pico_framework_ring_buffer STATIC
    ring_buffer.c
)

add_library(pico_framework::ring_buffer ALIAS pico_framework_ring_buffer)

target_include_directories(pico_framework_ring_buffer PUBLIC
    include
)

pico_framework_set_warnings(ring_buffer.c)
