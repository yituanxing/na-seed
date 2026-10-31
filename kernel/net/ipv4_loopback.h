#ifndef NASEED_IPV4_LOOPBACK_H
#define NASEED_IPV4_LOOPBACK_H
#include <stdbool.h>
#include <stdint.h>

/* Canonical host-order address and port values; the Linux sockaddr_in ABI
 * adapter handles network byte order. No external NIC or IP routing exists. */
#define NAS_IPV4_ANY UINT32_C(0)
#define NAS_IPV4_LOOPBACK UINT32_C(0x7f000001)
struct nas_ipv4_endpoint { uint32_t address; uint16_t port; };

/* These operations are atomic on NaSeed's single-core kernel. Each socket
 * identity is a refcounted open-description SLOT, never a process-local FD.
 * A successful accept transfers one queued kernel description reference. */
void nas_ipv4_socket_init(unsigned slot);
bool nas_ipv4_socket_active(unsigned slot);
void nas_ipv4_socket_release(unsigned slot);
int nas_ipv4_socket_bind(unsigned slot,struct nas_ipv4_endpoint local);
int nas_ipv4_socket_listen(unsigned slot,unsigned backlog);
int nas_ipv4_socket_connect(unsigned slot,struct nas_ipv4_endpoint remote);
int nas_ipv4_socket_accept(unsigned listener,unsigned *accepted);
int nas_ipv4_socket_name(unsigned slot,bool peer,
                         struct nas_ipv4_endpoint *out);
bool nas_ipv4_socket_listening(unsigned slot);
bool nas_ipv4_socket_readable(unsigned slot);
/* SO_REUSEADDR is meaningful for conflicting bound endpoints. An active
 * listener or connected endpoint always owns its port exclusively. */
int nas_ipv4_socket_reuseaddr(unsigned slot,bool set,bool *value);
#endif
