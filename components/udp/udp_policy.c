#include <stdio.h>
#include <string.h>

#include "udp_policy.h"

const char *udp_endpoint_result_name(udp_endpoint_result_t result)
{
    switch (result) {
        case UDP_ENDPOINT_OK:      return "ok";
        case UDP_ENDPOINT_NO_HOST: return "no host";
        case UDP_ENDPOINT_NO_PORT: return "port 0 cannot be addressed";
        default:                   return "unknown";
    }
}

udp_endpoint_result_t udp_check_endpoint(const char *host, uint16_t port)
{
    if (host == NULL || host[0] == '\0') {
        return UDP_ENDPOINT_NO_HOST;
    }
    if (port == 0) {
        return UDP_ENDPOINT_NO_PORT;
    }
    return UDP_ENDPOINT_OK;
}

bool udp_ipv4_parse(const char *text, uint8_t out[4])
{
    if (text == NULL || out == NULL) {
        return false;
    }

    uint8_t octets[4];
    const char *p = text;

    for (int i = 0; i < 4; i++) {
        if (i > 0) {
            if (*p != '.') {
                return false;
            }
            p++;
        }

        if (*p < '0' || *p > '9') {
            return false;
        }

        /*
         * No leading zeros. "010" is eight to inet_aton() and ten to everyone
         * reading the configuration, and there is no reading of it that is
         * worth guessing at.
         */
        if (*p == '0' && p[1] >= '0' && p[1] <= '9') {
            return false;
        }

        unsigned value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            value = (value * 10u) + (unsigned)(*p - '0');
            p++;
            if (++digits > 3) {
                return false;
            }
        }
        if (value > 255u) {
            return false;
        }
        octets[i] = (uint8_t)value;
    }

    /* Nothing may follow the fourth octet: "1.2.3.4x" is not an address, and
       neither is "1.2.3.4.5". */
    if (*p != '\0') {
        return false;
    }

    memcpy(out, octets, sizeof(octets));
    return true;
}

size_t udp_ipv4_format(const uint8_t addr[4], char *out, size_t size)
{
    if (addr == NULL || out == NULL || size == 0) {
        return 0;
    }

    const int written = snprintf(out, size, "%u.%u.%u.%u",
                                 (unsigned)addr[0], (unsigned)addr[1],
                                 (unsigned)addr[2], (unsigned)addr[3]);
    if (written < 0 || (size_t)written >= size) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

bool udp_ipv4_is_broadcast(const uint8_t addr[4])
{
    if (addr == NULL) {
        return false;
    }
    return addr[0] == 255u && addr[1] == 255u && addr[2] == 255u && addr[3] == 255u;
}

bool udp_ipv4_is_multicast(const uint8_t addr[4])
{
    if (addr == NULL) {
        return false;
    }
    return (addr[0] & 0xF0u) == 224u;
}

bool udp_payload_fits(size_t length)
{
    return length <= UDP_MAX_PAYLOAD;
}
