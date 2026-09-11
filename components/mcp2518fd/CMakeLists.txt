add_library(pico_framework_mcp2518fd STATIC
    mcp2518fd.c
    mcp2518fd_frame.c
    mcp2518fd_timing.c
)
add_library(pico_framework::mcp2518fd ALIAS pico_framework_mcp2518fd)
target_include_directories(pico_framework_mcp2518fd PUBLIC include)
pico_framework_set_warnings(mcp2518fd.c mcp2518fd_frame.c mcp2518fd_timing.c)
target_link_libraries(pico_framework_mcp2518fd PUBLIC
    pico_stdlib
    hardware_spi
    pico_framework::can_frame
)
