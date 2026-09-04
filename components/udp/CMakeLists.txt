# udp - poll-driven UDP datagrams over lwIP's raw API.
#
# udp_policy.c is addresses and endpoints as decisions: a strict dotted-quad
# parser, broadcast and multicast classification, and the payload bound. Free
# of lwIP and the Pico SDK, so it compiles straight into the host tests
# (DESIGN_DOC.md section 19) -- which is where it belongs, since the whole
# reason it exists is that lwIP's own parser accepts strings nobody means.
#
# udp.c binds the PCB, flattens incoming pbuf chains into one callback, and
# sends. It needs a link that is already up and being polled, which is wifi's
# job, not this component's -- DESIGN_DOC.md section 17 keeps higher-level
# networking separate from WiFi connection management.

add_library(pico_framework_udp STATIC
    udp_policy.c
    udp.c
)

add_library(pico_framework::udp ALIAS pico_framework_udp)

target_include_directories(pico_framework_udp PUBLIC
    include
)

pico_framework_set_warnings(udp_policy.c udp.c)

target_link_libraries(pico_framework_udp PUBLIC
    pico_framework::wifi
)

# lwIP's raw UDP API comes with the core stack that pico_cyw43_arch_lwip_poll
# already brings in through wifi, so there is no separate application library
# to link. On a board with no radio udp.c compiles to the same UDP_SUPPORTED=0
# stubs wifi.c uses for WIFI_SUPPORTED, so this component needs no conditional
# registration either.
