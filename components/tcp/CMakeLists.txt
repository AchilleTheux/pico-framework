# tcp - a poll-driven TCP client over lwIP's raw API.
#
# tcp_stream.c is the buffering and flow control between a connection and a
# byte stream -- free of lwIP and the Pico SDK, so it compiles straight into
# the host tests (DESIGN_DOC.md section 19). That is where the behaviour worth
# testing lives: partial sends, a full receive buffer, and what survives a
# disconnection.
#
# tcp.c drives lwIP's raw PCB: DNS, connect, a connect timeout, reconnection on
# the same exponential backoff wifi.c already implements and tests, and the
# receive path's back-pressure. It needs a link that is already up and being
# polled, which is wifi's job, not this component's -- DESIGN_DOC.md section 17
# keeps higher-level networking separate from WiFi connection management. This
# component depends on wifi only for that link and for lwipopts.h /
# pico_cyw43_arch_lwip_poll, the same way ax12 depends on half_duplex_uart for
# a transport it does not implement itself (section 7).

add_library(pico_framework_tcp STATIC
    tcp_stream.c
    tcp.c
)

add_library(pico_framework::tcp ALIAS pico_framework_tcp)

target_include_directories(pico_framework_tcp PUBLIC
    include
)

pico_framework_set_warnings(tcp_stream.c tcp.c)

target_link_libraries(pico_framework_tcp PUBLIC
    pico_framework::wifi
    pico_framework::ring_buffer
)

# lwIP's raw TCP API comes with the core stack that pico_cyw43_arch_lwip_poll
# already brings in through wifi, so unlike mqtt there is no separate
# application library to link here. On a board with no radio tcp.c compiles to
# the same TCP_SUPPORTED=0 stubs wifi.c uses for WIFI_SUPPORTED, so this
# component needs no conditional registration either.
