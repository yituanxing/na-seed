/* Linux NETLINK_ROUTE and narrowly supported NETLINK_SOCK_DIAG endpoints
 * backed by actual native lwIP state. Socket/OpenFile provider, not a VFS
 * file or fabricated success path. Only syscall adapter handles usercopy.
 *
 * Deliberately narrow: RTM_GETADDR(AF_INET/AF_INET6) and RTM_GETLINK
 * (AF_UNSPEC/AF_INET/AF_INET6), multipart NEWADDR/NEWLINK and DONE, kernel
 * source sockaddr_nl, genuine auto-assigned local port IDs.
 * Route multicast subscriptions are NOT implemented: a nonzero group bind
 * returns EPERM, which the unmodified dnsmasq v2.91 explicitly handles by
 * retrying with nl_groups=0. No made-up change notifications are sent. */
#include "netlink_route.h"
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
#define RTM_GETLINK_LINUX 18U
#define RTM_NEWADDR_LINUX 20U
#define RTM_GETADDR_LINUX 22U
#define NLMSG_DONE_LINUX 3U
#define NLM_F_REQUEST_LINUX 1U
#define NLM_F_MULTI_LINUX 2U
#define MSG_PEEK_LINUX 2U
#define MSG_TRUNC_LINUX 32U
#define MSG_DONTWAIT_LINUX 64U
#define NETLINK_REPLY_QUEUE (NAS_LWIP_GUEST_IF_LIMIT + 1U)
#define NETLINK_ROUTE_REPLY_SIZE 64U
/* Linux inet_diag_msg is 72 bytes, plus a 16-byte nlmsghdr. */
#define NETLINK_DIAG_REPLY_SIZE 96U
#define NETLINK_REPLY_SIZE NETLINK_DIAG_REPLY_SIZE
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
#define PORT_POLLIN 1U
#define PORT_POLLOUT 4U
#define PORT_POLLERR 8U

struct netlink_reply {
    uint8_t bytes[NETLINK_REPLY_SIZE];
    uint8_t length;
};
struct netlink_socket {
    bool owned,bound;
    uint8_t protocol;
    uint32_t owner_pid,port_id;
    uint32_t send_quota;
    uint32_t receive_quota;
    uint8_t head,count;
    struct netlink_reply replies[NETLINK_REPLY_QUEUE];
};
static struct netlink_socket sockets[NAS_OPEN_FILE_LIMIT];
static uint32_t next_port_id=UINT32_C(0x40000000);

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static struct netlink_socket *live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].owned &&
           nas_open_files[slot].refcount ? &sockets[slot]:NULL;
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
    if(sockets[slot].owned)return -EBUSY;
    sockets[slot]=(struct netlink_socket){.owned=true,.owner_pid=owner_pid,
        .protocol=(uint8_t)protocol,
        .send_quota=NAS_NETLINK_ROUTE_MAX_DATAGRAM,
        .receive_quota=protocol==NETLINK_SOCK_DIAG_LINUX ?
            NETLINK_REPLY_QUEUE*NETLINK_DIAG_REPLY_SIZE :
            NETLINK_REPLY_QUEUE*NETLINK_ROUTE_REPLY_SIZE};
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
    if(!s)return -EBADF;
    uint64_t doubled=(uint64_t)requested*2U;
    if(doubled<NETLINK_SNDBUF_MIN)doubled=NETLINK_SNDBUF_MIN;
    if(doubled>NETLINK_SNDBUF_MAX)doubled=NETLINK_SNDBUF_MAX;
    s->send_quota=(uint32_t)doubled;
    return 0;
}
int nas_netlink_route_get_sndbuf(unsigned slot,uint32_t *bytes) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->send_quota;
    return 0;
}
int nas_netlink_route_set_rcvbuf(unsigned slot,uint32_t requested) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    /* Linux's doubled socket quota is capped to memory this provider
     * ACTUALLY owns. No accepted SO_RCVBUF can reserve phantom records. */
    uint64_t doubled=(uint64_t)requested*2U;
    if(doubled<reply_allocation(s))doubled=reply_allocation(s);
    if(doubled>receive_capacity(s))doubled=receive_capacity(s);
    s->receive_quota=(uint32_t)doubled;
    return 0;
}
int nas_netlink_route_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->receive_quota;
    return 0;
}

static bool port_used(uint32_t pid,unsigned protocol) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].owned && sockets[i].bound &&
           sockets[i].protocol==protocol && sockets[i].port_id==pid)return true;
    return false;
}
static int allocate_port(struct netlink_socket *s,uint32_t requested) {
    if(s->bound)return -EINVAL;
    uint32_t port=requested ? requested:s->owner_pid;
    if(!port || port_used(port,s->protocol)) {
        if(requested)return -EADDRINUSE;
        /* Linux autobind can choose a distinct high port when the TGID is
         * already allocated. Never reuse a live kernel endpoint identity. */
        bool found=false;
        for(unsigned attempt=0;attempt<NAS_OPEN_FILE_LIMIT+1U;attempt++) {
            port=next_port_id++;
            if(!port || port_used(port,s->protocol))continue;
            found=true;break;
        }
        if(!found)return -EADDRINUSE;
    }
    s->port_id=port;s->bound=true;
    return 0;
}
static int parse_addr(const uint8_t *addr,unsigned length,
                      uint32_t *pid,uint32_t *groups) {
    if(!addr || length<NAS_NETLINK_ROUTE_SOCKADDR_SIZE)return -EINVAL;
    if(get16(addr)!=AF_NETLINK_LINUX)return -EAFNOSUPPORT;
    if(get16(addr+2))return -EINVAL;
    if(pid)*pid=get32(addr+4);
    if(groups)*groups=get32(addr+8);
    return 0;
}
static void make_addr(uint8_t addr[NAS_NETLINK_ROUTE_SOCKADDR_SIZE],
                      uint32_t port,uint32_t groups) {
    memset(addr,0,NAS_NETLINK_ROUTE_SOCKADDR_SIZE);
    put16(addr,AF_NETLINK_LINUX);
    put32(addr+4,port);put32(addr+8,groups);
}
int nas_netlink_route_bind(unsigned slot,const uint8_t *address,unsigned length) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    uint32_t pid=0,groups=0;
    int rc=parse_addr(address,length,&pid,&groups);
    if(rc)return rc;
    /* We cannot honor multicast route subscriptions: report that rather
     * than pretend to deliver async routing change notifications. */
    if(groups)return -EPERM;
    return allocate_port(s,pid);
}
int nas_netlink_route_getsockname(unsigned slot,
                                 uint8_t address[NAS_NETLINK_ROUTE_SOCKADDR_SIZE]) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!address)return -EINVAL;
    make_addr(address,s->bound?s->port_id:0,0);
    return 0;
}

static struct netlink_reply *enqueue(struct netlink_socket *s) {
    if(s->count==NETLINK_REPLY_QUEUE)return NULL;
    struct netlink_reply *reply=
        &s->replies[(s->head+s->count)%NETLINK_REPLY_QUEUE];
    memset(reply,0,sizeof(*reply));
    ++s->count;
    return reply;
}
static bool can_enqueue(const struct netlink_socket *s,unsigned records) {
    /* Account each protocol's complete allocated reply record, even when
     * its wire payload is smaller than the fixed-slot backing storage. */
    return (unsigned)s->count+records<=NETLINK_REPLY_QUEUE &&
           ((uint64_t)s->count+records)*reply_allocation(s)<=s->receive_quota;
}
static void make_nlmsg(uint8_t *out,uint32_t length,uint16_t type,
                       uint32_t seq,uint32_t port) {
    put32(out,length);put16(out+4,type);put16(out+6,NLM_F_MULTI_LINUX);
    put32(out+8,seq);put32(out+12,port);
}
static void enqueue_addr(struct netlink_socket *s,uint32_t seq,
                         uint32_t ifindex,uint8_t prefix,uint8_t scope,
                         uint32_t ipv4) {
    struct netlink_reply *r=enqueue(s);
    /* 16 nlmsghdr + 8 ifaddrmsg + 8 IFA_ADDRESS + 8 IFA_LOCAL. */
    r->length=40;
    make_nlmsg(r->bytes,r->length,RTM_NEWADDR_LINUX,seq,s->port_id);
    r->bytes[16]=AF_INET_LINUX;
    r->bytes[17]=prefix;
    r->bytes[18]=0; /* ifa_flags: none */
    r->bytes[19]=scope; /* 254 host scope for loopif, 0 universe for NIC */
    put32(r->bytes+20,ifindex);
    for(unsigned a=0;a<2;a++) {
        const unsigned off=24+a*8;
        put16(r->bytes+off,8);put16(r->bytes+off+2,(uint16_t)(a+1));
        r->bytes[off+4]=(uint8_t)(ipv4>>24);
        r->bytes[off+5]=(uint8_t)(ipv4>>16);
        r->bytes[off+6]=(uint8_t)(ipv4>>8);
        r->bytes[off+7]=(uint8_t)ipv4;
    }
}
static void enqueue_link(struct netlink_socket *s,uint32_t seq,
                         const struct nas_lwip_guest_if *iface) {
    struct netlink_reply *r=enqueue(s);
    /* Linux UAPI: nlmsghdr(16) + ifinfomsg(16) + IFLA_IFNAME + IFLA_MTU.
     * IFNAMSIZ bounds the padded record to 60 bytes inside the 64-byte
     * reply slot. All attributes come from the one native lwIP snapshot. */
    const unsigned name_len=(unsigned)strnlen(iface->name,
                                             NAS_LWIP_DEVICE_NAME_MAX)+1U;
    const unsigned padded_name=(4U+name_len+3U)&~3U;
    const unsigned mtu_off=32U+padded_name;
    r->length=(uint8_t)(mtu_off+8U);
    make_nlmsg(r->bytes,r->length,RTM_NEWLINK_LINUX,seq,s->port_id);
    r->bytes[16]=AF_UNSPEC_LINUX;
    put16(r->bytes+18,iface->loopback ? ARPHRD_LOOPBACK_LINUX :
                            ARPHRD_ETHER_LINUX);
    put32(r->bytes+20,iface->ifindex);
    uint32_t flags=iface->up ? IFF_UP_LINUX:0;
    if(iface->link_up)flags|=IFF_RUNNING_LINUX|IFF_LOWER_UP_LINUX;
    if(iface->loopback)flags|=IFF_LOOPBACK_LINUX;
    put32(r->bytes+24,flags);
    put32(r->bytes+28,0); /* Snapshot, not a link-state change message. */
    put16(r->bytes+32,(uint16_t)(4U+name_len));
    put16(r->bytes+34,IFLA_IFNAME_LINUX);
    memcpy(r->bytes+36,iface->name,name_len);
    put16(r->bytes+mtu_off,8);
    put16(r->bytes+mtu_off+2,IFLA_MTU_LINUX);
    put32(r->bytes+mtu_off+4,iface->mtu);
}
static void enqueue_done(struct netlink_socket *s,uint32_t seq) {
    struct netlink_reply *r=enqueue(s);
    /* Linux multipart NLMSG_DONE carries an int error (zero on success).
     * Stock iproute2 rtnl_dump_done rejects a header-only record as a
     * truncated reply. enqueue() zeroes the payload, including error=0. */
    r->length=20;
    make_nlmsg(r->bytes,r->length,NLMSG_DONE_LINUX,seq,
               s->protocol==NETLINK_SOCK_DIAG_LINUX ? 0:s->port_id);
}
static void enqueue_tcp_listener(struct netlink_socket *s,uint32_t seq,
                                 const struct nas_lwip_tcp_listener *listener) {
    struct netlink_reply *r=enqueue(s);
    /* Linux UAPI inet_diag_msg: nlmsghdr(16), family/state/timer(4),
     * inet_diag_sockid(48), expires/rqueue/wqueue/uid/inode(20). The TCP
     * endpoint comes from Netconn's post-listen GETNAME result. There is
     * no fabricated socket for an empty host/guest listener table. */
    r->length=88;
    make_nlmsg(r->bytes,r->length,SOCK_DIAG_BY_FAMILY_LINUX,seq,0);
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
    put32(r->bytes+64,UINT32_MAX);
    put32(r->bytes+68,UINT32_MAX);
    /* A LISTEN socket is considered only when its native accept queue is
     * empty (socket_port fails the dump otherwise), so rqueue is really 0.
     * Backlog was actually committed by Netconn's listen completion. */
    put32(r->bytes+76,listener->backlog);
    /* NaSeed's current accepted credential model is root-only. The OpenFile
     * slot+1 matches the actual /proc/pid/fd socket:[N] identity. */
    put32(r->bytes+84,listener->slot+1U);
}
static int64_t send_diag(struct netlink_socket *s,const uint8_t *bytes,
                         unsigned length,uint32_t seq) {
    const uint32_t msg_len=get32(bytes);
    const uint16_t flags=get16(bytes+6);
    /* SOCK_DIAG_BY_FAMILY + inet_diag_req_v2: 16+56 bytes. Only an
     * unfiltered TCP LISTEN dump is currently represented truthfully. */
    if(get16(bytes+4)!=SOCK_DIAG_BY_FAMILY_LINUX || msg_len!=72U ||
       !(flags&NLM_F_REQUEST_LINUX) || (flags&0x300U)!=0x300U)
        return -EOPNOTSUPP;
    const uint8_t family=bytes[16],protocol=bytes[17];
    const uint32_t states=get32(bytes+20);
    if((family!=AF_INET_LINUX && family!=AF_INET6_LINUX) ||
       protocol!=IPPROTO_TCP_LINUX || bytes[18] || bytes[19])
        return -EOPNOTSUPP;
    /* Reporting no ESTABLISHED/etc sockets would be a false full result:
     * this provider has only an auditable native LISTEN inventory. */
    if(family==AF_INET_LINUX && (states&~(1U<<TCP_LISTEN_LINUX)))
        return -EOPNOTSUPP;
    /* The base request's local/remote filters and optional NLA bytecode
     * are unsupported; refusing them prevents an incorrect full dump. */
    for(unsigned i=24;i<64U;i++)if(bytes[i])return -EOPNOTSUPP;
    /* A wildcard dump commonly carries the Linux NOCOOKIE sentinel.
     * Accept both zero-initialized and INET_DIAG_NOCOOKIE identities. */
    for(unsigned i=64;i<72U;i++)
        if(bytes[i]!=0 && bytes[i]!=UINT8_MAX)return -EOPNOTSUPP;
    if((get32(bytes+64)!=0 && get32(bytes+64)!=UINT32_MAX) ||
       (get32(bytes+68)!=0 && get32(bytes+68)!=UINT32_MAX))
        return -EOPNOTSUPP;
    if(!s->bound) {
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
    if(length>NAS_NETLINK_ROUTE_MAX_DATAGRAM || length>s->send_quota)
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
        rc=parse_addr(destination,destination_length,&target,&groups);
    else if(destination_length)
        rc=-EINVAL;
    if(rc)return rc;
    if(target || groups)return -EOPNOTSUPP; /* only real kernel endpoint */
    const uint32_t msg_len=get32(bytes);
    const uint16_t type=get16(bytes+4),flags=get16(bytes+6);
    const uint32_t seq=get32(bytes+8);
    if(msg_len<17 || msg_len>length)return -EINVAL;
    if(s->protocol==NETLINK_SOCK_DIAG_LINUX)
        return send_diag(s,bytes,length,seq);
    if((type!=RTM_GETADDR_LINUX && type!=RTM_GETLINK_LINUX) ||
       !(flags&NLM_F_REQUEST_LINUX))
        return -EOPNOTSUPP;
    if(type==RTM_GETADDR_LINUX && msg_len<20)return -EINVAL;
    if(type==RTM_GETLINK_LINUX && msg_len<32)return -EINVAL;
    const unsigned family=bytes[16];
    if(family!=AF_UNSPEC_LINUX && family!=AF_INET_LINUX &&
       family!=AF_INET6_LINUX)
        return -EAFNOSUPPORT;

    if(type==RTM_GETLINK_LINUX) {
        struct nas_lwip_guest_if links[NAS_LWIP_GUEST_IF_LIMIT]={0};
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
        }
        if(!can_enqueue(s,count+1U))return -EAGAIN;
        if(!s->bound) {
            rc=allocate_port(s,0);
            if(rc)return rc;
        }
        for(unsigned i=0;i<count;i++)enqueue_link(s,seq,&links[i]);
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
    if(!s->bound) {
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
    if(flags&~(MSG_PEEK_LINUX|MSG_TRUNC_LINUX|MSG_DONTWAIT_LINUX))
        return -EOPNOTSUPP;
    if(capacity && !destination)return -EINVAL;
    if(!s->count)return -EAGAIN;
    struct netlink_reply *r=&s->replies[s->head];
    unsigned copied=capacity<r->length ? capacity:r->length;
    if(copied)memcpy(destination,r->bytes,copied);
    if(source)make_addr(source,0,0);
    if(source_length)*source_length=NAS_NETLINK_ROUTE_SOCKADDR_SIZE;
    if(msg_flags)*msg_flags=copied<r->length ? MSG_TRUNC_LINUX:0;
    const unsigned actual=r->length;
    if(!(flags&MSG_PEEK_LINUX)) {
        s->head=(uint8_t)((s->head+1U)%NETLINK_REPLY_QUEUE);
        --s->count;
    }
    return (flags&MSG_TRUNC_LINUX) ? actual:copied;
}

static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct netlink_socket *s=live(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    if(!s->count)return (uint64_t)-EAGAIN;
    return requested<s->replies[s->head].length ? requested:
           s->replies[s->head].length;
}
static uint64_t read_bytes(unsigned slot,uint8_t *out,uint64_t count) {
    return (uint64_t)nas_netlink_route_recvmsg(slot,out,(unsigned)count,
                                               0,NULL,NULL,NULL);
}
static uint16_t poll_description(unsigned slot,uint16_t events) {
    struct netlink_socket *s=live(slot);
    if(!s)return PORT_POLLERR;
    uint16_t ready=0;
    if(s->count)ready|=PORT_POLLIN;
    if(can_enqueue(s,1))ready|=PORT_POLLOUT;
    return (uint16_t)(ready&events);
}
static uint16_t poll_fd(uint64_t fd,uint16_t events) {
    if(!nas_current_task || !nas_current_task->fds || fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_SOCKET)return 0;
    return poll_description(nas_current_task->fds->entries[fd].slot,events);
}
static void release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)
        sockets[slot]=(struct netlink_socket){0};
}
static const struct nas_file_ops ops={
    .read_plan=read_plan,.read=read_bytes,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=poll_fd,.poll_description=poll_description,.release=release
};
const struct nas_file_ops *nas_netlink_route_ops(void){return &ops;}
