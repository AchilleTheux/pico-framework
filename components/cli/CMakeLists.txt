# Line-oriented command interpreter.
#
# cli.c is transport-agnostic and calls no Pico SDK function, so the host tests
# compile it directly. The cli_stream_*.c files are the only SDK-dependent part.

add_library(pico_framework_cli STATIC
    cli.c
    cli_stream_stdio.c
    cli_stream_uart.c
)

add_library(pico_framework::cli ALIAS pico_framework_cli)

target_include_directories(pico_framework_cli PUBLIC
    include
)

pico_framework_set_warnings(cli.c cli_stream_stdio.c cli_stream_uart.c)

target_link_libraries(pico_framework_cli PUBLIC
    pico_stdlib
    hardware_uart
)
