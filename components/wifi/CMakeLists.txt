# Station-mode connection management for the Pico W's CYW43 radio.
#
# wifi_policy.c is the retry schedule and the credential checks, free of the SDK
# and compiled straight into the host tests — which is where the interesting
# behaviour is, since the alternative is testing reconnection by unplugging an
# access point and waiting.
#
# wifi.c compiles on every board. Without a radio it becomes a set of stubs
# returning WIFI_ERR_UNSUPPORTED, so the component need not be conditionally
# registered and the build matrix stays uniform.

add_library(pico_framework_wifi STATIC
    wifi_policy.c
    wifi.c
)

add_library(pico_framework::wifi ALIAS pico_framework_wifi)

target_include_directories(pico_framework_wifi PUBLIC
    include
)

pico_framework_set_warnings(wifi_policy.c wifi.c)

target_link_libraries(pico_framework_wifi PUBLIC
    pico_stdlib
)

# The radio libraries only exist for boards that have one.
if (PICO_CYW43_SUPPORTED)
    # Poll mode rather than threadsafe_background: lwIP runs only when
    # wifi_poll() is called, which keeps the radio on the same footing as every
    # other component here and out of interrupt context. The cost is that a
    # caller must poll; the benefit is no concurrency to reason about.
    target_link_libraries(pico_framework_wifi PUBLIC
        pico_cyw43_arch_lwip_poll
    )

    # lwIP will not compile without an lwipopts.h from the application, and has
    # no usable defaults of its own. The one in include/ is PUBLIC so that
    # linking this component is enough; an application needing different
    # settings puts its own earlier on the include path.
endif()
