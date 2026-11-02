/* Linux NETLINK_ROUTE dump endpoint backed by *actual* native lwIP IPv4
 * interface state. This is a socket/OpenFile provider, not a VFS file or a
 * fabricated success path. The syscall adapter alone handles user pointers.
 *
 * Deliberately narrow: RTM_GETADDR(AF_INET/AF_INET6), multipart NEWADDR and
 * DONE, kernel source sockaddr_nl, genuine auto-assigned local port IDs.
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

#define AF_NETLINK_LINUX 16U
#define AF_INET_LINUX 2U
#define AF_INET6_LINUX 10U
#define RTM_NEWADDR_LINUX 20U
#define RTM_GETADDR_LINUX 22U
#define NLMSG_DONE_LINUX 3U
#define NLM_F_REQUEST_LINUX 1U
#define NLM_F_MULTI_LINUX 2U
#define MSG_PEEK_LINUX 2U
#define MSG_TRUNC_LINUX 32U
#define MSG_DONTWAIT_LINUX 64U
#define NETLINK_REPLY_QUEUE (NAS_LWIP_GUEST_IF_LIMIT + 1U)
#define NETLINK_REPLY_SIZE 64U
#define PORT_POLLIN 1U
#define PORT_POLLOUT 4U
#define PORT_POLLERR 8U

struct netlink_reply {
    uint8_t bytes[NETLINK_REPLY_SIZE];
    uint8_t length;
};
struct netlink_socket {
    bool owned,bound;
    uint32_t owner_pid,port_id;
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

int nas_netlink_route_open(unsigned slot,uint32_t owner_pid) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(sockets[slot].owned)return -EBUSY;
    sockets[slot]=(struct netlink_socket){.owned=true,.owner_pid=owner_pid};
    return 0;
}

static bool port_used(uint32_t pid) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].owned && sockets[i].bound &&
           sockets[i].port_id==pid)return true;
    return false;
}
static int allocate_port(struct netlink_socket *s,uint32_t requested) {
    if(s->bound)return -EINVAL;
    uint32_t port=requested ? requested:s->owner_pid;
    if(!port || port_used(port)) {
        if(requested)return -EADDRINUSE;
        /* Linux autobind can choose a distinct high port when the TGID is
         * already allocated. Never reuse a live kernel endpoint identity. */
        bool found=false;
        for(unsigned attempt=0;attempt<NAS_OPEN_FILE_LIMIT+1U;attempt++) {
            port=next_port_id++;
            if(!port || port_used(port))continue;
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
static void enqueue_done(struct netlink_socket *s,uint32_t seq) {
    struct netlink_reply *r=enqueue(s);
    r->length=16;
    make_nlmsg(r->bytes,r->length,NLMSG_DONE_LINUX,seq,s->port_id);
}

int64_t nas_netlink_route_sendto(unsigned slot,const uint8_t *bytes,
                                unsigned length,const uint8_t *destination,
                                unsigned destination_length) {
    struct netlink_socket *s=live(slot);
    if(!s)return -EBADF;
    if(length>NAS_NETLINK_ROUTE_MAX_DATAGRAM)return -EMSGSIZE;
    if(!bytes || length<20)return -EINVAL;
    if(!destination)return -EDESTADDRREQ;
    uint32_t target=0,groups=0;
    int rc=parse_addr(destination,destination_length,&target,&groups);
    if(rc)return rc;
    if(target || groups)return -EOPNOTSUPP; /* only real kernel endpoint */
    const uint32_t msg_len=get32(bytes);
    const uint16_t type=get16(bytes+4),flags=get16(bytes+6);
    const uint32_t seq=get32(bytes+8);
    if(msg_len<20 || msg_len>length)return -EINVAL;
    if(type!=RTM_GETADDR_LINUX || !(flags&NLM_F_REQUEST_LINUX))
        return -EOPNOTSUPP;
    const unsigned family=bytes[16];
    if(family!=AF_INET_LINUX && family!=AF_INET6_LINUX)
        return -EAFNOSUPPORT;

    struct nas_lwip_guest_ipv4_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
    const unsigned count=family==AF_INET_LINUX ?
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
    if(s->count+count+1U>NETLINK_REPLY_QUEUE)return -EAGAIN;
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
    if(s->count<NETLINK_REPLY_QUEUE)ready|=PORT_POLLOUT;
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
