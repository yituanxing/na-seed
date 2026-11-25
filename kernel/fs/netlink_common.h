#ifndef NASEED_NETLINK_COMMON_H
#define NASEED_NETLINK_COMMON_H
#include "netlink_core.h"
#include "../fd.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../lib/endian.h"
#include "../mm/heap.h"

/* Linux LP64 Netlink wire primitives. All supported ISAs are little-endian;
 * use byte accesses so unaligned attributes are safe. */
#define nas_nl_rd16 nas_get_le16
#define nas_nl_rd32 nas_get_le32
#define nas_nl_wr16 nas_put_le16
#define nas_nl_wr32 nas_put_le32
static inline unsigned nas_nl_align4(unsigned n) {return (n+3U)&~3U;}
static inline int nas_nl_parse_address(const uint8_t *p,unsigned n,
                                       uint32_t *port,uint32_t *groups) {
    if(!p || n<NAS_NETLINK_SOCKADDR_SIZE)return -EINVAL;
    if(nas_nl_rd16(p)!=16U)return -EAFNOSUPPORT;
    if(nas_nl_rd16(p+2))return -EINVAL;
    if(port)*port=nas_nl_rd32(p+4);
    if(groups)*groups=nas_nl_rd32(p+8);
    return 0;
}
static inline void nas_nl_make_address(uint8_t *p,uint32_t port,uint32_t groups) {
    memset(p,0,NAS_NETLINK_SOCKADDR_SIZE);
    nas_nl_wr16(p,16U);nas_nl_wr32(p+4,port);nas_nl_wr32(p+8,groups);
}
static inline uint32_t nas_nl_quota(uint32_t requested,uint32_t min,uint32_t max) {
    uint64_t n=(uint64_t)requested*2U;
    if(n<min)n=min;
    if(n>max)n=max;
    return (uint32_t)n;
}
/* Allocation remains protocol-local: ROUTE and SOCK_DIAG share the
 * provider but not their port namespace. Never reuse an owned endpoint. */
static inline int nas_nl_bind_port(bool *bound,uint32_t *port_id,
    uint32_t owner_pid,uint32_t requested,uint32_t *next_port,
    unsigned protocol,bool (*used)(uint32_t,unsigned)) {
    if(*bound)return -EINVAL;
    uint32_t port=requested?requested:owner_pid;
    if(!port || used(port,protocol)) {
        if(requested)return -EADDRINUSE;
        bool found=false;
        for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT+1U;i++) {
            port=(*next_port)++;
            if(port && !used(port,protocol)){found=true;break;}
        }
        if(!found)return -EADDRINUSE;
    }
    *port_id=port;*bound=true;
    return 0;
}
/* Caller handles queue state, overflow and MSG_PEEK consumption. */
static inline unsigned nas_nl_copy_reply(uint8_t *dst,unsigned capacity,
    const uint8_t *data,unsigned actual,unsigned flags,uint8_t *source,
    uint32_t groups,uint32_t *source_length,uint32_t *msg_flags) {
    unsigned copy=capacity<actual?capacity:actual;
    if(copy)memcpy(dst,data,copy);
    if(source)nas_nl_make_address(source,0,groups);
    if(source_length)*source_length=NAS_NETLINK_SOCKADDR_SIZE;
    if(msg_flags)*msg_flags=copy<actual?32U:0U; /* MSG_TRUNC */
    return flags&32U?actual:copy;
}
/* Common socket accounting and bounded reply-ring policy. Protocols retain
 * their own port namespace, payload backing, multicast and error semantics. */
/* Netlink protocols share a File-owned socket allocation. Each provider
 * supplies its real FileOps identity, preventing unrelated private unions
 * (e.g. eventfd counters) from being mistaken for a Netlink socket. */
static inline void *nas_nl_private(unsigned slot,const struct nas_file_ops *ops) {
    if(slot>=NAS_OPEN_FILE_LIMIT || nas_open_files[slot].ops!=ops)return NULL;
    return nas_open_files[slot].private.netlink;
}
static inline int nas_nl_socket_alloc(unsigned slot,const struct nas_file_ops *ops,
                                      size_t bytes,void **out) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(!nas_file_slot_live(slot) || nas_open_files[slot].ops!=ops)return -EBADF;
    if(nas_open_files[slot].private.netlink)return -EBUSY;
    void *p=nas_heap_calloc(1,bytes);
    if(!p)return -ENOMEM;
    nas_open_files[slot].private.netlink=p;
    *out=p;
    return 0;
}
static inline void nas_nl_socket_release(unsigned slot,
                                          const struct nas_file_ops *ops) {
    void *p=nas_nl_private(slot,ops);
    if(!p)return;
    nas_open_files[slot].private.netlink=NULL;
    (void)nas_heap_free(p);
}
struct nas_nl_endpoint {
    bool owned,bound;
    uint32_t owner_pid,port_id,send_quota,receive_quota;
    uint8_t head,count;
};
static inline bool nas_nl_live(unsigned slot,const struct nas_nl_endpoint *e) {
    return e->owned && nas_open_files[slot].refcount;
}
static inline int nas_nl_name(const struct nas_nl_endpoint *e,uint8_t *address,
                              uint32_t groups) {
    if(!e)return -EBADF;
    if(!address)return -EINVAL;
    nas_nl_make_address(address,e->bound?e->port_id:0,groups);
    return 0;
}
static inline int nas_nl_get_quota(const struct nas_nl_endpoint *e,
                                  bool receive,uint32_t *bytes) {
    if(!e)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=receive?e->receive_quota:e->send_quota;
    return 0;
}
static inline int nas_nl_set_quota(struct nas_nl_endpoint *e,bool receive,
    uint32_t requested,uint32_t min,uint32_t max) {
    if(!e)return -EBADF;
    if(receive)e->receive_quota=nas_nl_quota(requested,min,max);
    else e->send_quota=nas_nl_quota(requested,min,max);
    return 0;
}
static inline bool nas_nl_room(const struct nas_nl_endpoint *e,
    unsigned records,unsigned limit,unsigned record_bytes) {
    const uint64_t count=(uint64_t)e->count+records;
    return count<=limit && count*record_bytes<=e->receive_quota;
}
static inline void nas_nl_header(uint8_t *p,uint32_t size,uint16_t type,
                                 uint16_t flags,uint32_t seq,uint32_t port) {
    nas_nl_wr32(p,size);nas_nl_wr16(p+4,type);nas_nl_wr16(p+6,flags);
    nas_nl_wr32(p+8,seq);nas_nl_wr32(p+12,port);
}
static inline int nas_nl_recv_check(unsigned capacity,const uint8_t *dst,
                                     unsigned flags) {
    if(flags&~(2U|32U|64U))return -EOPNOTSUPP;
    return capacity && !dst ? -EINVAL:0;
}
static inline unsigned nas_nl_receive_record(struct nas_nl_endpoint *e,
    unsigned limit,uint8_t *dst,unsigned capacity,const uint8_t *data,
    unsigned actual,unsigned flags,uint8_t *source,uint32_t groups,
    uint32_t *source_length,uint32_t *msg_flags) {
    unsigned result=nas_nl_copy_reply(dst,capacity,data,actual,flags,
                                      source,groups,source_length,msg_flags);
    if(!(flags&2U)) { /* MSG_PEEK retains the original record. */
        e->head=(uint8_t)((e->head+1U)%limit);
        --e->count;
    }
    return result;
}
/* Shared userspace-adapter-independent receive admission for ROUTE,
 * GENERIC and NETFILTER. The protocol retains its own queue, address
 * groups and asynchronous overrun ordering. */
static inline int64_t nas_nl_receive_checked(struct nas_nl_endpoint *e,
    unsigned limit,uint8_t *dst,unsigned capacity,const uint8_t *data,
    unsigned actual,unsigned flags,uint8_t *source,uint32_t groups,
    uint32_t *source_length,uint32_t *msg_flags,bool *overrun) {
    int rc=nas_nl_recv_check(capacity,dst,flags);
    if(rc)return rc;
    /* The ROUTE multicast overflow is a one-shot ENOBUFS before queued
     * datagrams, but invalid MSG flags still take precedence. */
    if(overrun && *overrun) {*overrun=false;return -ENOBUFS;}
    if(!e->count)return -EAGAIN;
    return nas_nl_receive_record(e,limit,dst,capacity,data,actual,
                                 flags,source,groups,source_length,msg_flags);
}
static inline uint64_t nas_nl_read_plan(const struct nas_nl_endpoint *e,
    uint64_t requested,unsigned length) {
    if(!requested)return 0;
    if(!e->count)return (uint64_t)-EAGAIN;
    return requested<length?requested:length;
}
static inline uint16_t nas_nl_poll(const struct nas_nl_endpoint *e,
    uint16_t events,bool room,bool overrun) {
    uint16_t ready=(e->count?1U:0U)|(room?4U:0U);
    return (uint16_t)((ready&events)|(overrun?8U:0U));
}
#endif
