# A serial console over Classic Bluetooth.
#
# bt_stream.c is the flow control between RFCOMM's packets and the CLI's byte
# stream — free of BTstack and of the SDK, and compiled straight into the host
# tests, which is the only way that logic gets exercised without pairing a
# laptop.
#
# bt_console.c compiles on every board. Without a radio it becomes stubs, so
# the component need not be conditionally registered.

add_library(pico_framework_bluetooth STATIC
    bt_stream.c
    bt_console.c
)

add_library(pico_framework::bluetooth ALIAS pico_framework_bluetooth)

target_include_directories(pico_framework_bluetooth PUBLIC
    include
)

pico_framework_set_warnings(bt_stream.c bt_console.c)

target_link_libraries(pico_framework_bluetooth PUBLIC
    pico_stdlib
    pico_framework::cli
    pico_framework::ring_buffer
)

if (PICO_CYW43_SUPPORTED)
    # btstack_config.h in include/ is PUBLIC, so linking this component is
    # enough — BTstack will not compile without one and the SDK does not
    # provide it, the same arrangement as lwIP's lwipopts.h.
    target_link_libraries(pico_framework_bluetooth PUBLIC
        pico_btstack_classic
        pico_btstack_cyw43
        pico_btstack_run_loop_async_context
    )

    # Which cyw43_arch provides the async context both the radio and BTstack
    # run on. Only one may be linked, so the choice has to be made once:
    #
    #   OFF  Bluetooth alone, no lwIP.
    #   ON   the wifi component is also linked and brings the lwIP variant.
    #
    # Linking both would define cyw43_arch_init() twice.
    option(PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI
           "The wifi component is also linked and provides the cyw43 arch" OFF)

    if (PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI)
        # In this mode the wifi component is genuinely a dependency, not merely
        # present: it provides the single cyw43 arch both halves of the radio
        # run on, and the lwipopts.h that lwIP will not compile without. A
        # static library sees only its own dependencies, not the application's,
        # so linking it here is what puts those headers on this library's
        # include path.
        target_link_libraries(pico_framework_bluetooth PUBLIC
            pico_framework::wifi
        )
    else()
        target_link_libraries(pico_framework_bluetooth PUBLIC
            pico_cyw43_arch_poll
        )

        # Without lwIP the cyw43 driver has to be told so, or its header pulls
        # in lwip/netif.h. PUBLIC because the SDK compiles its own sources into
        # the application target, so the definition must reach those too.
        target_compile_definitions(pico_framework_bluetooth PUBLIC
            CYW43_LWIP=0
        )
    endif()
endif()
