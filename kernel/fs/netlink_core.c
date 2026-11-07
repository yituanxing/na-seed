/* One Netlink protocol registry and common kernel-buffer dispatch. No Linux
 * usercopy or FD table access is permitted in this layer. A single physical
 * provider can serve several protocol namespaces (ROUTE and SOCK_DIAG). */
#include "netlink_core.h"
#include "netlink_route.h"
#include "netlink_filter.h"
#include "netlink_generic.h"
#include "../errno.h"

_Static_assert(NAS_NETLINK_ROUTE_SOCKADDR_SIZE==NAS_NETLINK_SOCKADDR_SIZE,
               "route sockaddr_nl must share Linux ABI");
_Static_assert(NAS_NETLINK_FILTER_SOCKADDR_SIZE==NAS_NETLINK_SOCKADDR_SIZE,
               "filter sockaddr_nl must share Linux ABI");
_Static_assert(NAS_NETLINK_GENERIC_SOCKADDR_SIZE==NAS_NETLINK_SOCKADDR_SIZE,
               "generic sockaddr_nl must share Linux ABI");
_Static_assert(NAS_NETLINK_ROUTE_MAX_DATAGRAM==NAS_NETLINK_MAX_DATAGRAM,
               "route datagram must share import budget");
_Static_assert(NAS_NETLINK_FILTER_MAX_DATAGRAM==NAS_NETLINK_MAX_DATAGRAM,
               "filter datagram must share import budget");
_Static_assert(NAS_NETLINK_GENERIC_MAX_DATAGRAM==NAS_NETLINK_MAX_DATAGRAM,
               "generic datagram must share import budget");

struct netlink_provider {
    unsigned protocol;
    bool (*is_slot)(unsigned);
    int (*open)(unsigned,uint32_t);
    const struct nas_file_ops *(*ops)(void);
    int (*get_protocol)(unsigned);
    int (*bind)(unsigned,const uint8_t *,unsigned);
    int (*name)(unsigned,uint8_t *);
    int (*sndbuf_set)(unsigned,uint32_t);
    int (*sndbuf_get)(unsigned,uint32_t *);
    int (*rcvbuf_set)(unsigned,uint32_t);
    int (*rcvbuf_get)(unsigned,uint32_t *);
    int64_t (*send)(unsigned,const uint8_t *,unsigned,const uint8_t *,unsigned);
    int64_t (*recv)(unsigned,uint8_t *,unsigned,unsigned,uint8_t *,uint32_t *,uint32_t *);
};

/* SOCK_DIAG and ROUTE share a provider, rather than being two different
 * owners of the same OpenFile slot. Their open functions set the actual
 * protocol discriminator inside the route provider. */
static const struct netlink_provider providers[]={
    { NAS_NETLINK_ROUTE_PROTOCOL, nas_netlink_route_is_slot,
      nas_netlink_route_open,nas_netlink_route_ops,nas_netlink_route_protocol,
      nas_netlink_route_bind,nas_netlink_route_getsockname,
      nas_netlink_route_set_sndbuf,nas_netlink_route_get_sndbuf,
      nas_netlink_route_set_rcvbuf,nas_netlink_route_get_rcvbuf,
      nas_netlink_route_sendto,nas_netlink_route_recvmsg },
    { NAS_NETLINK_SOCK_DIAG_PROTOCOL, nas_netlink_route_is_slot,
      nas_netlink_diag_open,nas_netlink_route_ops,nas_netlink_route_protocol,
      nas_netlink_route_bind,nas_netlink_route_getsockname,
      nas_netlink_route_set_sndbuf,nas_netlink_route_get_sndbuf,
      nas_netlink_route_set_rcvbuf,nas_netlink_route_get_rcvbuf,
      nas_netlink_route_sendto,nas_netlink_route_recvmsg },
    { NAS_NETLINK_FILTER_PROTOCOL, nas_netlink_filter_is_slot,
      nas_netlink_filter_open,nas_netlink_filter_ops,nas_netlink_filter_protocol,
      nas_netlink_filter_bind,nas_netlink_filter_getsockname,
      nas_netlink_filter_set_sndbuf,nas_netlink_filter_get_sndbuf,
      nas_netlink_filter_set_rcvbuf,nas_netlink_filter_get_rcvbuf,
      nas_netlink_filter_sendto,nas_netlink_filter_recvmsg },
    { NAS_NETLINK_GENERIC_PROTOCOL, nas_netlink_generic_is_slot,
      nas_netlink_generic_open,nas_netlink_generic_ops,nas_netlink_generic_protocol,
      nas_netlink_generic_bind,nas_netlink_generic_getsockname,
      nas_netlink_generic_set_sndbuf,nas_netlink_generic_get_sndbuf,
      nas_netlink_generic_set_rcvbuf,nas_netlink_generic_get_rcvbuf,
      nas_netlink_generic_sendto,nas_netlink_generic_recvmsg },
};

static const struct netlink_provider *by_protocol(unsigned protocol) {
    for(unsigned i=0;i<sizeof(providers)/sizeof(providers[0]);i++)
        if(providers[i].protocol==protocol)return &providers[i];
    return 0;
}
static const struct netlink_provider *by_slot(unsigned slot) {
    /* ROUTE and SOCK_DIAG map to one owner; skip its duplicate descriptor. */
    for(unsigned i=0;i<sizeof(providers)/sizeof(providers[0]);i++)
        if(providers[i].is_slot(slot))return &providers[i];
    return 0;
}
bool nas_netlink_core_supports(unsigned protocol) {
    return by_protocol(protocol)!=0;
}
bool nas_netlink_core_is_slot(unsigned slot) {
    return by_slot(slot)!=0;
}
int nas_netlink_core_open(unsigned slot,uint32_t owner_pid,unsigned protocol,
                          const struct nas_file_ops **file_ops) {
    const struct netlink_provider *p=by_protocol(protocol);
    if(!p)return -EPROTONOSUPPORT;
    if(!file_ops)return -EINVAL;
    *file_ops=p->ops();
    return p->open(slot,owner_pid);
}
int nas_netlink_core_protocol(unsigned slot) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->get_protocol(slot):-EBADF;
}
int nas_netlink_core_bind(unsigned slot,const uint8_t *addr,unsigned length) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->bind(slot,addr,length):-EBADF;
}
int nas_netlink_core_getsockname(unsigned slot,uint8_t address[12]) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->name(slot,address):-EBADF;
}
int nas_netlink_core_set_sndbuf(unsigned slot,uint32_t requested) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->sndbuf_set(slot,requested):-EBADF;
}
int nas_netlink_core_get_sndbuf(unsigned slot,uint32_t *bytes) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->sndbuf_get(slot,bytes):-EBADF;
}
int nas_netlink_core_set_rcvbuf(unsigned slot,uint32_t requested) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->rcvbuf_set(slot,requested):-EBADF;
}
int nas_netlink_core_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->rcvbuf_get(slot,bytes):-EBADF;
}
int64_t nas_netlink_core_sendto(unsigned slot,const uint8_t *bytes,
                               unsigned length,const uint8_t *destination,
                               unsigned destination_length) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->send(slot,bytes,length,destination,destination_length):-EBADF;
}
int64_t nas_netlink_core_recvmsg(unsigned slot,uint8_t *destination,
                                 unsigned capacity,unsigned flags,
                                 uint8_t source[12],uint32_t *source_length,
                                 uint32_t *msg_flags) {
    const struct netlink_provider *p=by_slot(slot);
    return p?p->recv(slot,destination,capacity,flags,source,source_length,msg_flags):
           -EBADF;
}
