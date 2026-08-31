# Levelled logging to one or more sinks.
#
# log_format.c is the levels and the line prefix, free of the SDK and compiled
# straight into the host tests. log.c is the sinks.

add_library(pico_framework_logging STATIC
    log_format.c
    log.c
)

add_library(pico_framework::logging ALIAS pico_framework_logging)

target_include_directories(pico_framework_logging PUBLIC
    include
)

# Calls below this level are compiled out, format strings included. Set it in a
# profile; the default keeps debug and above.
set(LOG_COMPILE_LEVEL "LOG_DEBUG_LEVEL" CACHE STRING
    "Lowest level compiled in: LOG_TRACE_LEVEL..LOG_NONE_LEVEL")

target_compile_definitions(pico_framework_logging PUBLIC
    LOG_COMPILE_LEVEL=${LOG_COMPILE_LEVEL}
)

pico_framework_set_warnings(log_format.c log.c)

target_link_libraries(pico_framework_logging PUBLIC
    pico_stdlib
    pico_framework::ring_buffer
)
