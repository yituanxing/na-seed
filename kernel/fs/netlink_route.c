/* Linux NETLINK_ROUTE and narrowly supported NETLINK_SOCK_DIAG endpoints
 * backed by actual native lwIP state. Socket/OpenFile provider, not a VFS
 * file or fabricated success path. Only syscall adapter handles usercopy.
 *
 * Deliberately narrow: RTM_GETADDR(AF_INET/AF_INET6) and RTM_GETLINK
 * (AF_UNSPEC/AF_INET/AF_INET6), multipart NEWADDR/NEWLINK and DONE, kernel
 * source sockaddr_nl, genuine auto-assigned local port IDs. RTMGRP_LINK
 * and RTMGRP_IPV4_IFADDR receive only actual lwIP netif transitions on its
 * tcpip_thread. Unsupported groups still fail, never silently subscribe. */
#include "netlink_route.h"
#include "netlink_common.h"
#include "file_ops.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../../ports/lwip/network_boot.h"
#include "../../ports/lwip/socket_port.h"

#define AF_NETLINK_LINUX 16U
#define AF_UNSPEC_LINUX 0U
#define AF_INET_LINUX 2U
#define AF_INET6_LINUX 10U
#define NETLINK_ROUTE_LINUX 0U
#define NETLINK_SOCK_DIAG_LINUX 4U
#define SOCK_DIAG_BY_FAMILY_LINUX 20U
#define IPPROTO_TCP_LINUX 6U
#define TCP_LISTEN_LINUX 10U
#define RTM_NEWLINK_LINUX 16U
#define RTM_DELLINK_LINUX 17U
#define RTM_GETLINK_LINUX 18U
#define RTM_NEWADDR_LINUX 20U
#define RTM_DELADDR_LINUX 21U
#define RTM_GETADDR_LINUX 22U
#define RTM_NEWQDISC_LINUX 36U
#define RTM_GETQDISC_LINUX 38U
#define NLMSG_ERROR_LINUX 2U
#define NLMSG_DONE_LINUX 3U
#define NLM_F_REQUEST_LINUX 1U
#define NLM_F_MULTI_LINUX 2U
#define NLM_F_DUMP_LINUX 0x300U
#define NETLINK_REPLY_QUEUE (NAS_LWIP_GUEST_IF_LIMIT + 1U)
#define RTMGRP_LINK_LINUX 1U
#define RTMGRP_IPV4_IFADDR_LINUX 16U
#define ROUTE_SUPPORTED_GROUPS (RTMGRP_LINK_LINUX|RTMGRP_IPV4_IFADDR_LINUX)
#define NETLINK_ROUTE_REPLY_SIZE 384U
/* Linux inet_diag_msg is 72 bytes, plus a 16-byte nlmsghdr. */
#define NETLINK_DIAG_REPLY_SIZE 96U
#define NETLINK_REPLY_SIZE NETLINK_ROUTE_REPLY_SIZE
/* SO_SNDBUF is an admission quota for the synchronous user->kernel request,
 * not a claim that the kernel has allocated this many queued TX bytes. */
#define NETLINK_SNDBUF_MIN 2048U
#define NETLINK_SNDBUF_MAX 262144U
#define ARPHRD_ETHER_LINUX 1U
#define ARPHRD_LOOPBACK_LINUX 772U
#define IFF_UP_LINUX 0x0001U
#define IFF_LOOPBACK_LINUX 0x0008U
#define IFF_RUNNING_LINUX 0x0040U
#define IFF_LOWER_UP_LINUX 0x10000U
#define IFLA_IFNAME_LINUX 3U
#define IFLA_MTU_LINUX 4U
#define IFLA_STATS_LINUX 7U
#define IFLA_STATS64_LINUX 23U
#define TCA_KIND_LINUX 1U
#define TC_H_ROOT_LINUX UINT32_MAX
#define RTNL_LINK_STATS32_FIELDS 24U
#define RTNL_LINK_STATS64_FIELDS 25U
/* Longest IFNAMSIZ-1 name (16 bytes including terminator): base 32 +
 * padded name 20 + MTU 8 + stats64 204 + stats32 100 = 364 bytes.
 * The queue's physical backing must hold every route datagram, even
 * when a SOCK_DIAG reservation is charged at its smaller 96-byte size. */
_Static_assert(NETLINK_ROUTE_REPLY_SIZE>=364U,
               "route reply must fit full Linux 64/32-bit NIC statistics");
_Static_assert(NETLINK_ROUTE_REPLY_SIZE>=NETLINK_DIAG_REPLY_SIZE,
               "route reply backing must fit inet_diag message");
#define PORT_POLLERR 8U

struct netlink_reply {
    uint8_t bytes[NETLINK_REPLY_SIZE];
    uint16_t length;
    uint32_t source_groups;
};
struct netlink_socket {
    struct nas_nl_endpoint ep;
    bool overrun;
    uint8_t protocol;
    uint32_t groups;
    struct netlink_reply replies[NETLINK_REPLY_QUEUE];
};
static struct netlink_socket sockets[NAS_OPEN_FILE_LIMIT];
static uint32_t next_port_id=UINT32_C(0x40000000);

static struct netlink_socket *live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && nas_nl_live(slot,&sockets[slot].ep)
           ? &sockets[slot]:NULL;
}
bool nas_netlink_route_is_slot(unsigned slot) {return live(slot)!=NULL;}

static unsigned reply_allocation(const struct netlink_socket *s) {
    return s->protocol==NETLINK_SOCK_DIAG_LINUX ?
           NETLINK_DIAG_REPLY_SIZE:NETLINK_ROUTE_REPLY_SIZE;
}
static unsigned receive_capacity(const struct netlink_socket *s) {
    return NETLINK_REPLY_QUEUE*reply_allocation(s);
}
static int open_netlink(unsigned slot,uint32_t owner_pid,unsigned protocol) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(sockets[slot].ep.owned)return -EBUSY;
    sockets[slot]=(struct netlink_socket){.protocol=(uint8_t)protocol,
        .ep={.owned=true,.owner_pid=owner_pid,
        .send_quota=NAS_NETLINK_ROUTE_MAX_DATAGRAM,
        .receive_quota=protocol==NETLINK_SOCK_DIAG_LINUX ?
            NETLINK_REPLY_QUEUE*NETLINK_DIAG_REPLY_SIZE :
            NETLINK_REPLY_QUEUE*NETLINK_ROUTE_REPLY_SIZE}};
    return 0;
}
int nas_netlink_route_open(unsigned slot,uint32_t owner_pid) {
    return open_netlink(slot,owner_pid,NETLINK_ROUTE_LINUX);
}
int nas_netlink_diag_open(unsigned slot,uint32_t owner_pid) {
    return open_netlink(slot,owner_pid,NETLINK_SOCK_DIAG_LINUX);
}
int nas_netlink_route_protocol(unsigned slot) {
    struct netlink_socket *s=live(slot);
    return s ? (int)s->protocol:-EBADF;
}

int nas_netlink_route_set_sndbuf(unsigned slot,uint32_t requested) {
    struct netlink_socket *s=live(slot);
    return nas_nl_set_quota(s?&s->ep:NULL,false,requested,NETLINK_SNDBUF_MIN,NETLINK_SNDBUF_MAX);
}
int nas_netlink_route_get_sndbuf(unsigned slot,uint32_t *bytes) {
    struct netlink_socket *s=live(slot);
    return nas_nl_get_quota(s?&s->ep:NULL,false,bytes);
}
int nas_netlink_route_set_rcvbuf(unsigned slot,uint32_t requested) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    return nas_nl_set_quota(&s->ep,true,requested,
                            reply_allocation(s),receive_capacity(s));
}
int nas_netlink_route_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    struct netlink_socket *s=live(slot);
    return nas_nl_get_quota(s?&s->ep:NULL,true,bytes);
}

static bool port_used(uint32_t pid,unsigned protocol) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].ep.owned && sockets[i].ep.bound &&
           sockets[i].protocol==protocol && sockets[i].ep.port_id==pid)return true;
    return false;
}
static int allocate_port(struct netlink_socket *s,uint32_t requested) {
    return nas_nl_bind_port(&s->ep.bound,&s->ep.port_id,s->ep.owner_pid,requested,
                            &next_port_id,s->protocol,port_used);
}
int nas_netlink_route_bind(unsigned slot,const uint8_t *address,unsigned length) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    uint32_t pid=0,groups=0;
    int rc=nas_nl_parse_address(address,length,&pid,&groups);
    if(rc)return rc;
    /* Linux nl_groups is a mask of group numbers, not a group number.
     * SOCK_DIAG has no supported multicast; reject unknown groups for
     * NETLINK_ROUTE rather than inventing event delivery. */
    if(groups&(uint32_t)~(s->protocol==NETLINK_ROUTE_LINUX ?
                          ROUTE_SUPPORTED_GROUPS:0U))return -EPERM;
    const int result=allocate_port(s,pid);
    if(!result)s->groups=groups;
    return result;
}
int nas_netlink_route_getsockname(unsigned slot,
                                 uint8_t address[NAS_NETLINK_ROUTE_SOCKADDR_SIZE]) {
    struct netlink_socket *s=live(slot);
    return nas_nl_name(s?&s->ep:NULL,address,s?s->groups:0);
}

static struct netlink_reply *enqueue(struct netlink_socket *s) {
    if(s->ep.count==NETLINK_REPLY_QUEUE)return NULL;
    struct netlink_reply *reply=
        &s->replies[(s->ep.head+s->ep.count)%NETLINK_REPLY_QUEUE];
    memset(reply,0,sizeof(*reply));
    ++s->ep.count;
    return reply;
}
static bool can_enqueue(const struct netlink_socket *s,unsigned records) {
    return nas_nl_room(&s->ep,records,NETLINK_REPLY_QUEUE,reply_allocation(s));
}
/* All NaSeed kernel C, including tcpip_thread callbacks, runs with local
 * IRQs masked and without kernel preemption on one CPU (k_sync.c contract).
 * Thus producer, recv, and final OpenFile release are serialized here. No
 * callback persists a slot pointer across scheduling. SMP/preemptible kernel
 * requires one shared irqsave lock covering these same queue operations. */
static struct netlink_reply *newest_reply(struct netlink_socket *s) {
    return &s->replies[(s->ep.head+s->ep.count-1U)%NETLINK_REPLY_QUEUE];
}
static void make_nlmsg(uint8_t *out,uint32_t length,uint16_t type,
                       uint32_t seq,uint32_t port) {
    nas_nl_header(out,length,type,NLM_F_MULTI_LINUX,seq,port);
}
static void enqueue_addr(struct netlink_socket *s,uint32_t seq,
                         uint32_t ifindex,uint8_t prefix,uint8_t scope,
                         uint32_t ipv4) {
    struct netlink_reply *r=enqueue(s);
    /* 16 nlmsghdr + 8 ifaddrmsg + 8 IFA_ADDRESS + 8 IFA_LOCAL. */
    r->length=40;
    make_nlmsg(r->bytes,r->length,RTM_NEWADDR_LINUX,seq,s->ep.port_id);
    r->bytes[16]=AF_INET_LINUX;
    r->bytes[17]=prefix;
    r->bytes[18]=0; /* ifa_flags: none */
    r->bytes[19]=scope; /* 254 host scope for loopif, 0 universe for NIC */
    nas_nl_wr32(r->bytes+20,ifindex);
    for(unsigned a=0;a<2;a++) {
        const unsigned off=24+a*8;
        nas_nl_wr16(r->bytes+off,8);nas_nl_wr16(r->bytes+off+2,(uint16_t)(a+1));
        r->bytes[off+4]=(uint8_t)(ipv4>>24);
        r->bytes[off+5]=(uint8_t)(ipv4>>16);
        r->bytes[off+6]=(uint8_t)(ipv4>>8);
        r->bytes[off+7]=(uint8_t)ipv4;
    }
}
static void enqueue_link(struct netlink_socket *s,uint32_t seq,
                         const struct nas_lwip_guest_if *iface,
                         const struct nas_netdev_stats *stats) {
    struct netlink_reply *r=enqueue(s);
    /* Linux UAPI: nlmsghdr(16) + ifinfomsg(16), name, MTU, optional
     * 25x64-bit and 24x32-bit link statistics. The latter are genuine
     * pinned-device counters captured before any multipart reply is queued.
     * There is presently no loopback byte/packet instrumentation, so omit
     * statistics attributes for lo rather than invent counters for it. */
    const unsigned name_len=(unsigned)strnlen(iface->name,
                                             NAS_LWIP_DEVICE_NAME_MAX)+1U;
    const unsigned padded_name=(4U+name_len+3U)&~3U;
    const unsigned mtu_off=32U+padded_name;
    const unsigned stats64_off=mtu_off+8U;
    const unsigned stats32_off=stats64_off+4U+RTNL_LINK_STATS64_FIELDS*8U;
    r->length=(uint16_t)(stats ? stats32_off+4U+
                        RTNL_LINK_STATS32_FIELDS*4U : stats64_off);
    make_nlmsg(r->bytes,r->length,RTM_NEWLINK_LINUX,seq,s->ep.port_id);
    r->bytes[16]=AF_UNSPEC_LINUX;
    nas_nl_wr16(r->bytes+18,iface->loopback ? ARPHRD_LOOPBACK_LINUX :
                            ARPHRD_ETHER_LINUX);
    nas_nl_wr32(r->bytes+20,iface->ifindex);
    uint32_t flags=iface->up ? IFF_UP_LINUX:0;
    if(iface->link_up)flags|=IFF_RUNNING_LINUX|IFF_LOWER_UP_LINUX;
    if(iface->loopback)flags|=IFF_LOOPBACK_LINUX;
    nas_nl_wr32(r->bytes+24,flags);
    nas_nl_wr32(r->bytes+28,0); /* Snapshot, not a link-state change message. */
    nas_nl_wr16(r->bytes+32,(uint16_t)(4U+name_len));
    nas_nl_wr16(r->bytes+34,IFLA_IFNAME_LINUX);
    memcpy(r->bytes+36,iface->name,name_len);
    nas_nl_wr16(r->bytes+mtu_off,8);
    nas_nl_wr16(r->bytes+mtu_off+2,IFLA_MTU_LINUX);
    nas_nl_wr32(r->bytes+mtu_off+4,iface->mtu);
    if(!stats)return;
    nas_nl_wr16(r->bytes+stats64_off,4U+RTNL_LINK_STATS64_FIELDS*8U);
    nas_nl_wr16(r->bytes+stats64_off+2,IFLA_STATS64_LINUX);
    nas_nl_wr16(r->bytes+stats32_off,4U+RTNL_LINK_STATS32_FIELDS*4U);
    nas_nl_wr16(r->bytes+stats32_off+2,IFLA_STATS_LINUX);
    /* struct rtnl_link_stats64 / rtnl_link_stats field order:
     * rx_packets,tx_packets,rx_bytes,tx_bytes,rx_errors,tx_errors,
     * rx_dropped,tx_dropped,... . Counters without device sources remain
     * zero, exactly as a Linux driver with no optional hardware counters.
     * Preserve full 64-bit values, truncate only the 32-bit compatibility
     * projection, and do not count IRQ events or rejected TX as packets. */
    const uint64_t fields[8]={
        stats->rx_packets,stats->tx_packets,
        stats->rx_bytes,stats->tx_bytes,
        0,stats->tx_errors,stats->rx_dropped,0
    };
    for(unsigned i=0;i<8;i++) {
        nas_put_le64(r->bytes+stats64_off+4U+i*8U,fields[i]);
        nas_nl_wr32(r->bytes+stats32_off+4U+i*4U,(uint32_t)fields[i]);
    }
}
/* No second NetDevice registry: the publisher supplies one already observed
 * upstream netif identity. These records contain no speculative counters,
 * and are only emitted by the real attach/status/detach lifecycle. */
static bool valid_event_if(const struct nas_lwip_guest_if *iface) {
    if(!iface || !iface->ifindex || !iface->mtu)return false;
    const unsigned n=(unsigned)strnlen(iface->name,NAS_LWIP_DEVICE_NAME_MAX);
    return n && n<NAS_LWIP_DEVICE_NAME_MAX;
}
void nas_netlink_route_publish_link(const struct nas_lwip_guest_if *iface,
                                   bool removed) {
    if(!valid_event_if(iface))return;
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        struct netlink_socket *s=live(slot);
        if(!s || s->protocol!=NETLINK_ROUTE_LINUX ||
           !(s->groups&RTMGRP_LINK_LINUX))continue;
        if(!can_enqueue(s,1)) {s->overrun=true;continue;}
        enqueue_link(s,0,iface,NULL);
        struct netlink_reply *r=newest_reply(s);
        nas_nl_wr16(r->bytes+4,removed ? RTM_DELLINK_LINUX:RTM_NEWLINK_LINUX);
        nas_nl_wr16(r->bytes+6,0); /* notification, not a multipart dump */
        nas_nl_wr32(r->bytes+12,0); /* kernel sender nl_pid */
        nas_nl_wr32(r->bytes+28,UINT32_MAX); /* ifi_change: complete state */
        r->source_groups=RTMGRP_LINK_LINUX;
    }
}
void nas_netlink_route_publish_ipv4(const struct nas_lwip_guest_if *iface,
                                   bool removed) {
    if(!valid_event_if(iface) || !iface->ipv4_host_order ||
       !iface->prefix_len || iface->prefix_len>32)return;
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        struct netlink_socket *s=live(slot);
        if(!s || s->protocol!=NETLINK_ROUTE_LINUX ||
           !(s->groups&RTMGRP_IPV4_IFADDR_LINUX))continue;
        if(!can_enqueue(s,1)) {s->overrun=true;continue;}
        enqueue_addr(s,0,iface->ifindex,iface->prefix_len,
                     iface->loopback ? 254U:0U,iface->ipv4_host_order);
        struct netlink_reply *r=newest_reply(s);
        nas_nl_wr16(r->bytes+4,removed ? RTM_DELADDR_LINUX:RTM_NEWADDR_LINUX);
        nas_nl_wr16(r->bytes+6,0);
        nas_nl_wr32(r->bytes+12,0);
        r->source_groups=RTMGRP_IPV4_IFADDR_LINUX;
    }
}
static void enqueue_done(struct netlink_socket *s,uint32_t seq) {
    struct netlink_reply *r=enqueue(s);
    /* Linux multipart NLMSG_DONE carries an int error (zero on success).
     * Stock iproute2 rtnl_dump_done rejects a header-only record as a
     * truncated reply. enqueue() zeroes the payload, including error=0. */
    r->length=20;
    /* A multipart dump's nlmsg_pid is the receiving socket's assigned
     * local port, even though the sender sockaddr_nl reports kernel pid 0.
     * iproute2 libnetlink rejects every mismatched nlmsg_pid, including
     * NLMSG_DONE, then loops waiting for a completion it never accepts. */
    make_nlmsg(r->bytes,r->length,NLMSG_DONE_LINUX,seq,s->ep.port_id);
}
static void enqueue_unsupported_qdisc(struct netlink_socket *s,uint32_t seq,
                                       const uint8_t *request) {
    struct netlink_reply *r=enqueue(s);
    /* rtnetlink sends a NLMSG_ERROR reply for unsupported requests. A
     * synchronous sendmsg(-EOPNOTSUPP) is NOT equivalent: libnl advances its
     * expected sequence only on NLMSG_ERROR/NLMSG_DONE. bmon requests an
     * optional qdisc cache before the next RTM_GETLINK cache refresh; a
     * missing error record therefore desynchronizes all later dumps.
     * We have no real qdisc inventory, so report the limitation honestly. */
    r->length=36U; /* nlmsghdr + nlmsgerr.error + original nlmsghdr */
    make_nlmsg(r->bytes,r->length,NLMSG_ERROR_LINUX,seq,s->ep.port_id);
    nas_nl_wr16(r->bytes+6,0); /* an error record is not multipart */
    nas_nl_wr32(r->bytes+16,(uint32_t)-EOPNOTSUPP);
    memcpy(r->bytes+20,request,16);
}
static void enqueue_loopback_noqueue(struct netlink_socket *s,uint32_t seq,
                                     uint32_t ifindex) {
    struct netlink_reply *r=enqueue(s);
    /* Serialization of the actual per-interface attached noqueue policy.
     * Its direct-output callback hands packets to upstream lwIP, whose
     * internal pbuf queue is unrelated to Linux TC queuing. */
    r->length=48U;
    make_nlmsg(r->bytes,r->length,RTM_NEWQDISC_LINUX,seq,s->ep.port_id);
    nas_nl_wr32(r->bytes+20,ifindex); /* tcmsg.tcm_ifindex */
    nas_nl_wr32(r->bytes+28,TC_H_ROOT_LINUX); /* tcmsg.tcm_parent */
    nas_nl_wr16(r->bytes+36,12U);
    nas_nl_wr16(r->bytes+38,TCA_KIND_LINUX);
    memcpy(r->bytes+40,"noqueue",8U);
}
static void enqueue_tcp_listener(struct netlink_socket *s,uint32_t seq,
                                 const struct nas_lwip_tcp_listener *listener) {
    struct netlink_reply *r=enqueue(s);
    /* Linux UAPI inet_diag_msg: nlmsghdr(16), family/state/timer(4),
     * inet_diag_sockid(48), expires/rqueue/wqueue/uid/inode(20). The TCP
     * endpoint comes from Netconn's post-listen GETNAME result. There is
     * no fabricated socket for an empty host/guest listener table. */
    r->length=88;
    make_nlmsg(r->bytes,r->length,SOCK_DIAG_BY_FAMILY_LINUX,seq,s->ep.port_id);
    r->bytes[16]=AF_INET_LINUX;
    r->bytes[17]=TCP_LISTEN_LINUX;
    r->bytes[20]=(uint8_t)(listener->local.port>>8);
    r->bytes[21]=(uint8_t)listener->local.port;
    const uint32_t ipv4=listener->local.ipv4;
    r->bytes[24]=(uint8_t)(ipv4>>24);
    r->bytes[25]=(uint8_t)(ipv4>>16);
    r->bytes[26]=(uint8_t)(ipv4>>8);
    r->bytes[27]=(uint8_t)ipv4;
    /* inet_diag_sockid places ifindex at +40 and its two cookie words at
     * +44/+48 (offsets 64/68 including the netlink header and diag prefix).
     * Keep ifindex zero; an invented interface number is incorrect. */
    nas_nl_wr32(r->bytes+64,UINT32_MAX);
    nas_nl_wr32(r->bytes+68,UINT32_MAX);
    /* A LISTEN socket is considered only when its native accept queue is
     * empty (socket_port fails the dump otherwise), so rqueue is really 0.
     * Backlog was actually committed by Netconn's listen completion. */
    nas_nl_wr32(r->bytes+76,listener->backlog);
    /* NaSeed's current accepted credential model is root-only. The OpenFile
     * slot+1 matches the actual /proc/pid/fd socket:[N] identity. */
    nas_nl_wr32(r->bytes+84,listener->slot+1U);
}
static int64_t send_diag(struct netlink_socket *s,const uint8_t *bytes,
                         unsigned length,uint32_t seq) {
    const uint32_t msg_len=nas_nl_rd32(bytes);
    const uint16_t flags=nas_nl_rd16(bytes+6);
    /* SOCK_DIAG_BY_FAMILY + inet_diag_req_v2: 16+56 bytes. Only an
     * unfiltered TCP LISTEN dump is currently represented truthfully. */
    if(nas_nl_rd16(bytes+4)!=SOCK_DIAG_BY_FAMILY_LINUX || msg_len!=72U ||
       !(flags&NLM_F_REQUEST_LINUX) || (flags&0x300U)!=0x300U)
        return -EOPNOTSUPP;
    const uint8_t family=bytes[16],protocol=bytes[17];
    const uint32_t states=nas_nl_rd32(bytes+20);
    if((family!=AF_INET_LINUX && family!=AF_INET6_LINUX) ||
       protocol!=IPPROTO_TCP_LINUX || bytes[18] || bytes[19])
        return -EOPNOTSUPP;
    /* iproute2 ss -ltn requests LISTEN|CLOSE (0x480): Linux does not keep
     * closed TCP endpoints in this listener inventory. Allow CLOSE in that
     * exact query, but never claim to enumerate ESTABLISHED/etc sockets. */
    if(family==AF_INET_LINUX &&
       (states&~((1U<<TCP_LISTEN_LINUX)|(1U<<7U))))
        return -EOPNOTSUPP;
    /* The base request's local/remote filters and optional NLA bytecode
     * are unsupported; refusing them prevents an incorrect full dump. */
    for(unsigned i=24;i<64U;i++)if(bytes[i])return -EOPNOTSUPP;
    /* A wildcard dump commonly carries the Linux NOCOOKIE sentinel.
     * Accept both zero-initialized and INET_DIAG_NOCOOKIE identities. */
    for(unsigned i=64;i<72U;i++)
        if(bytes[i]!=0 && bytes[i]!=UINT8_MAX)return -EOPNOTSUPP;
    if((nas_nl_rd32(bytes+64)!=0 && nas_nl_rd32(bytes+64)!=UINT32_MAX) ||
       (nas_nl_rd32(bytes+68)!=0 && nas_nl_rd32(bytes+68)!=UINT32_MAX))
        return -EOPNOTSUPP;
    if(!s->ep.bound) {
        int rc=allocate_port(s,0);
        if(rc)return rc;
    }
    if(family==AF_INET6_LINUX || !(states&(1U<<TCP_LISTEN_LINUX))) {
        if(!can_enqueue(s,1))return -EAGAIN;
        enqueue_done(s,seq);
        return length;
    }
    struct nas_lwip_tcp_listener listeners[NAS_OPEN_FILE_LIMIT];
    int count=nas_lwip_tcp_listeners_snapshot(listeners,NAS_OPEN_FILE_LIMIT);
    if(count<0)return count;
    if(!can_enqueue(s,(unsigned)count+1U))return -EAGAIN;
    for(int i=0;i<count;i++)enqueue_tcp_listener(s,seq,&listeners[i]);
    enqueue_done(s,seq);
    return length;
}

int64_t nas_netlink_route_sendto(unsigned slot,const uint8_t *bytes,
                                unsigned length,const uint8_t *destination,
                                unsigned destination_length) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    if(length>NAS_NETLINK_ROUTE_MAX_DATAGRAM || length>s->ep.send_quota)
        return -EMSGSIZE;
    if(!bytes || length<17)return -EINVAL;
    uint32_t target=0,groups=0;
    /* Linux NETLINK_ROUTE send()/sendto(NULL) addresses the kernel's
     * default port ID zero. iproute2's rtnl_{addr,link}dump_req use this
     * ordinary socket path without supplying a sockaddr_nl. An explicit
     * address must still be validated; peer or multicast delivery is not
     * implemented by this kernel-owned endpoint. */
    int rc=0;
    if(destination)
        rc=nas_nl_parse_address(destination,destination_length,&target,&groups);
    else if(destination_length)
        rc=-EINVAL;
    if(rc)return rc;
    if(target || groups)return -EOPNOTSUPP; /* only real kernel endpoint */
    const uint32_t msg_len=nas_nl_rd32(bytes);
    const uint16_t type=nas_nl_rd16(bytes+4),flags=nas_nl_rd16(bytes+6);
    const uint32_t seq=nas_nl_rd32(bytes+8);
    if(msg_len<17 || msg_len>length)return -EINVAL;
    if(s->protocol==NETLINK_SOCK_DIAG_LINUX)
        return send_diag(s,bytes,length,seq);
    if(type==RTM_GETQDISC_LINUX && (flags&NLM_F_REQUEST_LINUX)) {
        /* Only device-scoped dumps of actually attached qdiscs are in
         * scope. We cannot claim the physical NICs have any TC model, or
         * that a wildcard dump enumerates all disciplines. */
        if(msg_len<36U)return -EINVAL;
        const uint32_t ifindex=nas_nl_rd32(bytes+20);
        const bool loopback_query=ifindex==1U &&
            (flags&0x300U)==0x300U && bytes[16]==AF_UNSPEC_LINUX &&
            nas_nl_rd32(bytes+24)==0 && nas_nl_rd32(bytes+28)==0 &&
            nas_nl_rd32(bytes+32)==0;
        struct nas_lwip_guest_qdisc qdiscs[NAS_LWIP_GUEST_IF_LIMIT]={0};
        const unsigned qdisc_count=loopback_query ?
            nas_lwip_guest_qdisc_snapshot(qdiscs) : 0;
        if(qdisc_count>NAS_LWIP_GUEST_IF_LIMIT)return -EIO;
        bool attached_noqueue=false;
        for(unsigned i=0;i<qdisc_count;i++)
            if(qdiscs[i].ifindex==ifindex &&
               qdiscs[i].kind==NAS_NET_QDISC_NOQUEUE)
                attached_noqueue=true;
        if(!can_enqueue(s,attached_noqueue ? 2U:1U))return -EAGAIN;
        if(!s->ep.bound) {
            rc=allocate_port(s,0);
            if(rc)return rc;
        }
        if(attached_noqueue) {
            enqueue_loopback_noqueue(s,seq,ifindex);
            enqueue_done(s,seq);
            return length;
        }
        enqueue_unsupported_qdisc(s,seq,bytes);
        return length;
    }
    if((type!=RTM_GETADDR_LINUX && type!=RTM_GETLINK_LINUX) ||
       !(flags&NLM_F_REQUEST_LINUX))
        return -EOPNOTSUPP;
    if(type==RTM_GETADDR_LINUX && msg_len<20)return -EINVAL;
    if(type==RTM_GETLINK_LINUX && msg_len<32) {
        /* musl getifaddrs() sends the valid 20-byte rtgenmsg form of
         * RTM_GETLINK, whereas iproute2 sends a 32-byte ifinfomsg. Both
         * request the same real link snapshot. Never reject the native
         * musl wire shape just because it lacks ifinfomsg fields. */
        if(msg_len!=20 || (flags&NLM_F_DUMP_LINUX)!=NLM_F_DUMP_LINUX ||
           bytes[17] || bytes[18] || bytes[19])return -EINVAL;
    }
    const unsigned family=bytes[16];
    if(family!=AF_UNSPEC_LINUX && family!=AF_INET_LINUX &&
       family!=AF_INET6_LINUX)
        return -EAFNOSUPPORT;

    if(type==RTM_GETLINK_LINUX) {
        struct nas_lwip_guest_if links[NAS_LWIP_GUEST_IF_LIMIT]={0};
        struct nas_netdev_stats stats[NAS_LWIP_GUEST_IF_LIMIT]={0};
        const unsigned count=nas_lwip_guest_if_snapshot(links);
        if(!count || count>NAS_LWIP_GUEST_IF_LIMIT)return -EIO;
        for(unsigned i=0;i<count;i++) {
            const struct nas_lwip_guest_if *iface=&links[i];
            const unsigned n=(unsigned)strnlen(iface->name,
                                                NAS_LWIP_DEVICE_NAME_MAX);
            if(!iface->ifindex || !n || n==NAS_LWIP_DEVICE_NAME_MAX ||
               !iface->mtu)return -EIO;
            if(i==0) {
                if(strcmp(iface->name,"lo") || iface->ifindex!=1 ||
                   !iface->loopback)return -EIO;
            } else if(iface->loopback || iface->ifindex==1)return -EIO;
            for(unsigned j=0;j<i;j++)
                if(links[j].ifindex==iface->ifindex)return -EIO;
            if(!iface->loopback) {
                /* Exactly one authoritative one-shot Ethernet statistics
                 * snapshot per link: no re-resolution after reply enqueue.
                 * A concurrent NIC removal must not yield a partial dump. */
                struct nas_netdev *dev=nas_netdev_get(iface->name);
                if(!dev)return -ENODEV;
                int read_rc=nas_netdev_stats_snapshot(dev,&stats[i]);
                int put_rc=nas_netdev_put(dev);
                if(read_rc || put_rc)return read_rc ? read_rc:put_rc;
            }
        }
        if(!can_enqueue(s,count+1U))return -EAGAIN;
        if(!s->ep.bound) {
            rc=allocate_port(s,0);
            if(rc)return rc;
        }
        for(unsigned i=0;i<count;i++)
            enqueue_link(s,seq,&links[i],
                         links[i].loopback ? NULL:&stats[i]);
        enqueue_done(s,seq);
        return length;
    }

    struct nas_lwip_guest_ipv4_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
    /* AF_UNSPEC is an IPv4+IPv6 family request. Our native stack currently
     * publishes only genuine IPv4 leases; report those, never synthesize
     * absent IPv6 addresses or an empty supposedly successful dump. */
    const unsigned count=family!=AF_INET6_LINUX ?
        nas_lwip_guest_ipv4_ifconf(interfaces):0;
    if(count>NAS_LWIP_GUEST_IF_LIMIT)return -EIO;
    /* Reject an impossible or unverified core snapshot; never issue a
     * successful but misleading empty interface dump. */
    for(unsigned i=0;i<count;i++) {
        if(!interfaces[i].ifindex || !interfaces[i].ipv4_host_order ||
           interfaces[i].prefix_len>32)
            return -EIO;
        if(i==0) {
            if(strcmp(interfaces[i].name,"lo") ||
               interfaces[i].ifindex!=1 ||
               interfaces[i].ipv4_host_order!=UINT32_C(0x7f000001) ||
               interfaces[i].prefix_len!=8)return -EIO;
        } else if(interfaces[i].ifindex==1)return -EIO;
    }
    if(!can_enqueue(s,count+1U))return -EAGAIN;
    if(!s->ep.bound) {
        rc=allocate_port(s,0);
        if(rc)return rc;
    }
    for(unsigned i=0;i<count;i++)
        enqueue_addr(s,seq,interfaces[i].ifindex,interfaces[i].prefix_len,
                     i==0 ? 254U:0U,interfaces[i].ipv4_host_order);
    enqueue_done(s,seq);
    return length;
}

int64_t nas_netlink_route_recvmsg(unsigned slot,uint8_t *destination,
                                 unsigned capacity,unsigned flags,
                                 uint8_t source[NAS_NETLINK_ROUTE_SOCKADDR_SIZE],
                                 uint32_t *source_length,uint32_t *msg_flags) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    int rc=nas_nl_recv_check(capacity,destination,flags);
    if(rc)return rc;
    /* Linux reports an asynchronous multicast receive overflow as one
     * ENOBUFS. This is not a successful empty read, and retained queued
     * datagrams remain readable on the following call. */
    if(s->overrun) {s->overrun=false;return -ENOBUFS;}
    if(!s->ep.count)return -EAGAIN;
    struct netlink_reply *r=&s->replies[s->ep.head];
    return nas_nl_receive_record(&s->ep,NETLINK_REPLY_QUEUE,destination,capacity,
        r->bytes,r->length,flags,source,r->source_groups,source_length,msg_flags);
}

static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct netlink_socket *s=live(slot);
    if(!s)return (uint64_t)-EBADF;
    if(requested && s->overrun)return (uint64_t)-ENOBUFS;
    return nas_nl_read_plan(&s->ep,requested,s->replies[s->ep.head].length);
}
static uint64_t read_bytes(unsigned slot,uint8_t *out,uint64_t count) {
    return (uint64_t)nas_netlink_route_recvmsg(slot,out,(unsigned)count,
                                               0,NULL,NULL,NULL);
}
static uint16_t poll_description(unsigned slot,uint16_t events) {
    struct netlink_socket *s=live(slot);
    return s?nas_nl_poll(&s->ep,events,can_enqueue(s,1),s->overrun):PORT_POLLERR;
}
static void release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)
        sockets[slot]=(struct netlink_socket){0};
}
static const struct nas_file_ops ops={
    .read_plan=read_plan,.read=read_bytes,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll_description=poll_description,.release=release
};
const struct nas_file_ops *nas_netlink_route_ops(void){return &ops;}
