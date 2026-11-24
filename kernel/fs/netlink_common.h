#ifndef NASEED_NETLINK_COMMON_H
#define NASEED_NETLINK_COMMON_H
#include "netlink_core.h"
#include "../fd.h"
#include "../errno.h"
#include "../lib/string.h"

/* Linux LP64 Netlink wire primitives. All supported ISAs are little-endian;
 * use byte accesses so unaligned attributes are safe. */
static inline uint16_t nas_nl_rd16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static inline uint32_t nas_nl_rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline void nas_nl_wr16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static inline void nas_nl_wr32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
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
#endif
