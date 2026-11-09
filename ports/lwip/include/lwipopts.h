#ifndef NASEED_LWIPOPTS_H
#define NASEED_LWIPOPTS_H
/* The official stack does ALL IP/ARP/ICMP/DHCP/DNS/TCP/UDP work.
 * Per-mailbox queue capacities must remain <= NaSeed k_mailbox (8 entries).
 * Concurrent Netconns/PCBs are independent memory pools, not mailboxes. */
#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_COMPAT_MUTEX 1
#define LWIP_TCPIP_CORE_LOCKING 0
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#define LWIP_NETCONN 1
#define LWIP_SOCKET 0
/* Original Net-SNMP UDP agent needs actual per-packet destination and
 * ingress netif metadata for Linux IP_PKTINFO recvmsg ancillary. */
#define LWIP_NETBUF_RECVINFO 1
#define LWIP_ERRNO_INCLUDE <nas_lwip_errno.h>
#define LWIP_NETIF_API 1
#define LWIP_NETIF_STATUS_CALLBACK 1 /* verify DHCP lease, not DHCP start */
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1 /* ACK callback also on unchanged-IP DHCP renew */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_DHCP 1
#define LWIP_DNS 1
/* Keep upstream DHCP option-6 decoding and DNS resolver integration.
 * Exactly two addresses fit the default-route procfs atomic snapshot. */
#define DNS_MAX_SERVERS 2
#define LWIP_DHCP_MAX_DNS_SERVERS 2
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 1
/* Alpine HTTPS APKINDEX traffic measured a 2144-byte default receive window
 * (4 * TCP_MSS, 536-byte MSS) and about 24KiB/s signed-index transfer.
 * Double the advertised window without exceeding the eight-entry native
 * TCP receive mailbox's worst-case MSS-sized packet capacity. A separate
 * close-under-pressure test caught lost admitted bytes with an 8KiB window;
 * do not increase the window without resolving the mailbox/backpressure
 * boundary and rerunning the real TCP close drain. */
#define TCP_WND (4U * 1024U)
#define SO_REUSE 1 /* native SO_REUSEADDR for real Alpine ncat listeners */
#define IP_SOF_BROADCAST 1 /* native UDP SO_BROADCAST on official IP PCB */
#define LWIP_ARP 1
#define LWIP_RAW 1
#define LWIP_LOOPBACK_MAX_PBUFS 8
#define LWIP_NETIF_LOOPBACK 1
#define TCPIP_MBOX_SIZE 8
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8
#define MEMP_NUM_NETCONN 32
/* Back each admitted Netconn with a real upstream PCB. Without these pools,
 * raising only MEMP_NUM_NETCONN would report sockets that cannot open. */
#define MEMP_NUM_TCP_PCB 32
#define MEMP_NUM_UDP_PCB 32
#define MEMP_NUM_TCP_PCB_LISTEN 32
#define MEMP_NUM_TCP_SEG 128
/* Upstream's default MEMP_NUM_NETBUF=2 cannot hold even one musl A+AAAA
 * burst while the Netconn worker is constructing an outgoing datagram.
 * Every queued UDP receive consumes a Netbuf until userspace reads it;
 * cover the configured 64-packet pbuf pool plus eight transient SEND/RX
 * allocations, instead of silently dropping the second valid DNS reply. */
#define MEMP_NUM_NETBUF 72
#define PBUF_POOL_SIZE 64
#define TCPIP_THREAD_STACKSIZE 8192
#define TCPIP_THREAD_PRIO 1
#define DEFAULT_THREAD_STACKSIZE 8192
#define MEM_ALIGNMENT 8
#define MEM_SIZE (256 * 1024)
#define LWIP_STATS 0
#define LWIP_DEBUG 0
#define LWIP_CHECKSUM_ON_COPY 0
#endif
