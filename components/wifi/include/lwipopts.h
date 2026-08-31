/*
 * lwipopts.h - lwIP configuration for the framework's wifi component.
 *
 * lwIP has no defaults of its own worth the name: it expects the application to
 * supply this file, and will not compile without one. Providing it here means a
 * caller gets a working stack by linking the component, rather than having to
 * find a copy of this from an example and guess which of its two hundred
 * settings matter.
 *
 * Tuned for what this component does: one station-mode interface, DHCP, TCP and
 * UDP, no sockets and no threads. An application that needs more can put its
 * own lwipopts.h earlier on the include path and this one will be ignored.
 */

#ifndef PICO_FRAMEWORK_LWIPOPTS_H
#define PICO_FRAMEWORK_LWIPOPTS_H

/*
 * No operating system. Everything runs from wifi_poll() on the caller's thread,
 * which is what keeps the radio out of interrupt context and means there is no
 * concurrency in the stack to reason about.
 */
#define NO_SYS 1

/* The socket and netconn APIs both need threads. */
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

/* lwIP gets its own pool rather than malloc, so its memory use is bounded and
   known at link time. */
#define MEM_LIBC_MALLOC 0
#define MEMP_MEM_MALLOC 0
#define MEM_ALIGNMENT 4

/*
 * 4 KiB of heap and 24 packet buffers. Enough for a couple of TCP connections
 * and comfortable on either part's RAM; raise MEM_SIZE first if a transfer
 * stalls under load.
 */
#define MEM_SIZE 4000
#define PBUF_POOL_SIZE 24

#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1

/* DHCP, because a robot on someone else's network has no business assuming an
   address. */
#define LWIP_DHCP 1

/*
 * Skip the ARP probe DHCP would otherwise do before accepting its lease. It
 * costs a second or two of startup to protect against an address conflict that
 * a DHCP server has already ruled out.
 */
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#define TCP_MSS 1460
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

/* Needed by wifi_config_t's hostname, which is how a robot becomes findable by
   name instead of by hunting for its address. */
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1

/* Statistics and asserts cost flash and are off by default; turn them on while
   chasing a stack problem. */
#ifndef LWIP_STATS
#define LWIP_STATS 0
#endif
#ifndef LWIP_DEBUG
#define LWIP_DEBUG 0
#endif
#define LWIP_CHECKSUM_CTRL_PER_NETIF 1

#endif /* PICO_FRAMEWORK_LWIPOPTS_H */
