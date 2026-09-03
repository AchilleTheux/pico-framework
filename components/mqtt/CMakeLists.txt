# mqtt - a thin, poll-driven wrapper around lwIP's native MQTT client.
#
# mqtt_policy.c is topic and client-id validation -- free of lwIP and the Pico
# SDK, so it compiles straight into the host tests (DESIGN_DOC.md section 19).
#
# mqtt.c drives lwIP's mqtt_client_t: DNS, connect, reconnect on the same
# exponential backoff wifi.c already implements and tests, subscribe, publish,
# and reassembling a fragmented incoming publish into one callback. It needs a
# link that is already up and being polled, which is wifi's job, not this
# component's -- DESIGN_DOC.md section 17 keeps higher-level networking
# separate from WiFi connection management. This component depends on wifi
# only for that link and for lwipopts.h/pico_cyw43_arch_lwip_poll, the same way
# ax12 depends on half_duplex_uart for a transport it does not implement
# itself (section 7).

add_library(pico_framework_mqtt STATIC
    mqtt_policy.c
    mqtt.c
)

add_library(pico_framework::mqtt ALIAS pico_framework_mqtt)

target_include_directories(pico_framework_mqtt PUBLIC
    include
)

pico_framework_set_warnings(mqtt_policy.c mqtt.c)

target_link_libraries(pico_framework_mqtt PUBLIC
    pico_framework::wifi
)

# lwIP's MQTT client is only useful where the radio (and with it lwIP) is; on
# any other board mqtt.c compiles to the same MQTT_SUPPORTED=0 stubs wifi.c
# uses for WIFI_SUPPORTED, so this component needs no conditional registration
# either.
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_framework_mqtt PUBLIC
        pico_lwip_mqtt
    )
endif()
