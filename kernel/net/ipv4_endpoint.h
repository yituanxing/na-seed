#ifndef NASEED_IPV4_ENDPOINT_H
#define NASEED_IPV4_ENDPOINT_H

#include <stdint.h>

/* Shared host-order endpoint value. Linux sockaddr_in conversion belongs to
 * the syscall adapter; neither the legacy loopback stream nor upstream lwIP
 * owns this representation. */
#define NAS_IPV4_ANY UINT32_C(0)
#define NAS_IPV4_LOOPBACK UINT32_C(0x7f000001)
struct nas_ipv4_endpoint { uint32_t address; uint16_t port; };

#endif
