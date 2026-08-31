# Key/value settings that survive power-off.
#
# config_store.c is the record format and all the manipulation, free of the SDK
# and compiled straight into the host tests. persistent_config.c adds the flash.

add_library(pico_framework_persistent_config STATIC
    config_store.c
    persistent_config.c
)

add_library(pico_framework::persistent_config ALIAS pico_framework_persistent_config)

target_include_directories(pico_framework_persistent_config PUBLIC
    include
)

pico_framework_set_warnings(config_store.c persistent_config.c)

target_link_libraries(pico_framework_persistent_config PUBLIC
    pico_framework::flash_storage
    pico_framework::crc
)
