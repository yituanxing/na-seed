#ifndef NASEED_LWIPOPTS_H
#define NASEED_LWIPOPTS_H
/* The official stack does ALL IP/ARP/ICMP/DHCP/DNS/TCP/UDP work.
 * Keep capacity <= actual NaSeed kernel k_mailbox (8 slots) until a larger
 * shared OS mailbox replaces it; never lie about a requested queue size. */
#define NO_SYS 0
#define SYS_LIGHTWEIGHT_PROT 1
#define LWIP_COMPAT_MUTEX 1
#define LWIP_TCPIP_CORE_LOCKING 0
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0
#define LWIP_NETCONN 1
#define LWIP_SOCKET 0
#define LWIP_ERRNO_INCLUDE <nas_lwip_errno.h>
#define LWIP_NETIF_API 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_ARP 1
#define LWIP_RAW 1
#define LWIP_LOOPBACK_MAX_PBUFS 8
#define LWIP_NETIF_LOOPBACK 1
#define TCPIP_MBOX_SIZE 8
#define DEFAULT_TCP_RECVMBOX_SIZE 8
#define DEFAULT_UDP_RECVMBOX_SIZE 8
#define DEFAULT_RAW_RECVMBOX_SIZE 8
#define DEFAULT_ACCEPTMBOX_SIZE 8
#define MEMP_NUM_NETCONN 8
#define TCPIP_THREAD_STACKSIZE 8192
#define TCPIP_THREAD_PRIO 1
#define DEFAULT_THREAD_STACKSIZE 8192
#define MEM_ALIGNMENT 8
#define MEM_SIZE (64 * 1024)
#define LWIP_STATS 0
#define LWIP_DEBUG 0
#define LWIP_CHECKSUM_ON_COPY 0
#endif
