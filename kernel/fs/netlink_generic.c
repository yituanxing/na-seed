/* Bounded Linux NETLINK_GENERIC controller provider.  The only registered
 * family is the controller (`nlctrl`, GENL_ID_CTRL). GETFAMILY supports
 * name/ID lookup and a truthful single-family dump. Unsupported generic
 * families and controller commands are rejected with NLMSG_ERROR; there
 * are no invented multicast subscriptions, notifications or operations.
 *
 * Wire constants/attributes: Linux include/uapi/linux/{netlink,genetlink}.h.
 * Kernel-buffer interfaces only; the socket ABI adapter owns all usercopy. */
#include "netlink_generic.h"
#include "file_ops.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../lib/string.h"

#define NL_AF_NETLINK 16U
#define NL_GENL_CTRL_ID 16U
#define NLMSG_ERROR 2U
#define NLMSG_DONE 3U
#define NLM_F_REQUEST 1U
#define NLM_F_MULTI 2U
#define NLM_F_ACK 4U
#define NLM_F_DUMP 0x300U
#define NLA_F_NESTED 0x8000U
#define NLA_TYPE_MASK 0x3fffU
#define GENL_CTRL_NEWFAMILY 1U
#define GENL_CTRL_GETFAMILY 3U
#define GENL_CTRL_FAMILY_ID 1U
#define GENL_CTRL_FAMILY_NAME 2U
#define GENL_CTRL_VERSION 3U
#define GENL_CTRL_HDRSIZE 4U
#define GENL_CTRL_MAXATTR 5U
#define GENL_CTRL_OPS 6U
#define GENL_CTRL_OP_ID 1U
#define GENL_CTRL_OP_FLAGS 2U
#define GENL_CAP_DO 2U
#define GENL_CAP_DUMP 4U
#define MSG_PEEK 2U
#define MSG_TRUNC 32U
#define MSG_DONTWAIT 64U
#define PORT_POLLIN 1U
#define PORT_POLLOUT 4U
#define PORT_POLLERR 8U
#define GENL_REPLY_LIMIT 8U
#define GENL_REPLY_BYTES 128U
#define GENL_SNDBUF_MIN 2048U
#define GENL_SNDBUF_MAX 262144U

struct genl_reply { uint16_t length; uint8_t data[GENL_REPLY_BYTES]; };
struct genl_socket {
    bool owned,bound;
    uint32_t owner_pid,port_id,send_quota,receive_quota;
    uint8_t head,count;
    struct genl_reply replies[GENL_REPLY_LIMIT];
};
static struct genl_socket sockets[NAS_OPEN_FILE_LIMIT];
static uint32_t next_port_id=UINT32_C(0x60000000);

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void wr16(uint8_t *p,uint16_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
}
static void wr32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
static unsigned align4(unsigned value) {return (value+3U)&~3U;}
static struct genl_socket *live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].owned &&
           nas_open_files[slot].refcount ? &sockets[slot]:NULL;
}
bool nas_netlink_generic_is_slot(unsigned slot) {return live(slot)!=NULL;}
int nas_netlink_generic_open(unsigned slot,uint32_t owner_pid) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return -EBADF;
    if(sockets[slot].owned)return -EBUSY;
    sockets[slot]=(struct genl_socket){.owned=true,.owner_pid=owner_pid,
        .send_quota=NAS_NETLINK_GENERIC_MAX_DATAGRAM,
        .receive_quota=GENL_REPLY_LIMIT*GENL_REPLY_BYTES};
    return 0;
}
int nas_netlink_generic_protocol(unsigned slot) {
    return live(slot)?(int)NAS_NETLINK_GENERIC_PROTOCOL:-EBADF;
}
static bool port_used(uint32_t port) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].owned && sockets[i].bound &&
           sockets[i].port_id==port)return true;
    return false;
}
static int autobind(struct genl_socket *s,uint32_t requested) {
    if(s->bound)return -EINVAL;
    uint32_t port=requested?requested:s->owner_pid;
    if(!port || port_used(port)) {
        if(requested)return -EADDRINUSE;
        bool found=false;
        for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT+1U;i++) {
            port=next_port_id++;
            if(port && !port_used(port)){found=true;break;}
        }
        if(!found)return -EADDRINUSE;
    }
    s->port_id=port;s->bound=true;
    return 0;
}
static int parse_address(const uint8_t *p,unsigned n,
                         uint32_t *port,uint32_t *groups) {
    if(!p || n<NAS_NETLINK_GENERIC_SOCKADDR_SIZE)return -EINVAL;
    if(rd16(p)!=NL_AF_NETLINK)return -EAFNOSUPPORT;
    if(rd16(p+2))return -EINVAL;
    *port=rd32(p+4);*groups=rd32(p+8);
    return 0;
}
static void make_address(uint8_t *p,uint32_t port) {
    memset(p,0,NAS_NETLINK_GENERIC_SOCKADDR_SIZE);
    wr16(p,NL_AF_NETLINK);wr32(p+4,port);
}
int nas_netlink_generic_bind(unsigned slot,const uint8_t *address,unsigned n) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    uint32_t port,groups;
    int rc=parse_address(address,n,&port,&groups);
    if(rc)return rc;
    /* The provider cannot deliver real nlctrl notify multicast events. */
    if(groups)return -EOPNOTSUPP;
    return autobind(s,port);
}
int nas_netlink_generic_getsockname(unsigned slot,uint8_t address[12]) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!address)return -EINVAL;
    make_address(address,s->bound?s->port_id:0);
    return 0;
}
int nas_netlink_generic_set_sndbuf(unsigned slot,uint32_t requested) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    uint64_t bytes=(uint64_t)requested*2U;
    if(bytes<GENL_SNDBUF_MIN)bytes=GENL_SNDBUF_MIN;
    if(bytes>GENL_SNDBUF_MAX)bytes=GENL_SNDBUF_MAX;
    s->send_quota=(uint32_t)bytes;
    return 0;
}
int nas_netlink_generic_get_sndbuf(unsigned slot,uint32_t *bytes) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->send_quota;
    return 0;
}
int nas_netlink_generic_set_rcvbuf(unsigned slot,uint32_t requested) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    uint64_t bytes=(uint64_t)requested*2U;
    if(bytes<GENL_REPLY_BYTES)bytes=GENL_REPLY_BYTES;
    if(bytes>GENL_REPLY_LIMIT*GENL_REPLY_BYTES)
        bytes=GENL_REPLY_LIMIT*GENL_REPLY_BYTES;
    s->receive_quota=(uint32_t)bytes;
    return 0;
}
int nas_netlink_generic_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->receive_quota;
    return 0;
}

static bool room(const struct genl_socket *s,unsigned records) {
    return s->count+records<=GENL_REPLY_LIMIT &&
           (s->count+records)*GENL_REPLY_BYTES<=s->receive_quota;
}
static struct genl_reply *header(struct genl_socket *s,unsigned size,
                                 uint16_t type,uint16_t flags,uint32_t seq) {
    struct genl_reply *r=&s->replies[(s->head+s->count)%GENL_REPLY_LIMIT];
    memset(r,0,sizeof(*r));
    s->count++;
    r->length=(uint16_t)size;
    wr32(r->data,size);wr16(r->data+4,type);wr16(r->data+6,flags);
    wr32(r->data+8,seq);wr32(r->data+12,s->port_id);
    return r;
}
static unsigned attribute(uint8_t *p,unsigned off,uint16_t type,
                           const uint8_t *payload,unsigned count) {
    const unsigned size=4U+count;
    wr16(p+off,(uint16_t)size);wr16(p+off+2,type);
    if(count)memcpy(p+off+4,payload,count);
    return off+align4(size);
}
static unsigned attribute_u16(uint8_t *p,unsigned off,uint16_t type,
                               uint16_t value) {
    uint8_t payload[2];wr16(payload,value);
    return attribute(p,off,type,payload,2);
}
static unsigned attribute_u32(uint8_t *p,unsigned off,uint16_t type,
                               uint32_t value) {
    uint8_t payload[4];wr32(payload,value);
    return attribute(p,off,type,payload,4);
}
static void family_reply(struct genl_socket *s,uint32_t seq,bool dump) {
    /* Linux ctrl_fill_info/ctrl_build_family_msg: controller NEWFAMILY
     * describes registered operations. Only GETFAMILY DO+DUMP exists here.
     * Do not advertise GETPOLICY or `notify` (neither is implemented). */
    struct genl_reply *r=header(s,0,NL_GENL_CTRL_ID,
                                dump?NLM_F_MULTI:0,seq);
    r->data[16]=GENL_CTRL_NEWFAMILY;
    r->data[17]=2; /* nlctrl's public family version */
    unsigned off=20;
    static const uint8_t name[]={ 'n','l','c','t','r','l',0 };
    off=attribute(r->data,off,GENL_CTRL_FAMILY_NAME,name,sizeof(name));
    off=attribute_u16(r->data,off,GENL_CTRL_FAMILY_ID,NL_GENL_CTRL_ID);
    off=attribute_u32(r->data,off,GENL_CTRL_VERSION,2U);
    off=attribute_u32(r->data,off,GENL_CTRL_HDRSIZE,0U);
    off=attribute_u32(r->data,off,GENL_CTRL_MAXATTR,GENL_CTRL_FAMILY_NAME);
    unsigned nested=off;
    off+=4;
    unsigned item=off;
    off+=4;
    off=attribute_u32(r->data,off,GENL_CTRL_OP_ID,GENL_CTRL_GETFAMILY);
    off=attribute_u32(r->data,off,GENL_CTRL_OP_FLAGS,
                      GENL_CAP_DO|GENL_CAP_DUMP);
    wr16(r->data+item,(uint16_t)(off-item));
    wr16(r->data+item+2,NLA_F_NESTED|1U);
    wr16(r->data+nested,(uint16_t)(off-nested));
    /* iproute2's ctrl printer first parses top-level rtnetlink attrs with
     * parse_rtattr (without nested-bit stripping), then parses entries
     * using parse_rtattr_nested. Keep OPS visible to that real consumer. */
    wr16(r->data+nested+2,GENL_CTRL_OPS);
    r->length=(uint16_t)off;
    wr32(r->data,off);
}
static void done(struct genl_socket *s,uint32_t seq) {
    struct genl_reply *r=header(s,20,NLMSG_DONE,NLM_F_MULTI,seq);
    wr32(r->data+16,0);
}
static void ack(struct genl_socket *s,const uint8_t request[16],int error) {
    struct genl_reply *r=header(s,36,NLMSG_ERROR,0,rd32(request+8));
    wr32(r->data+16,(uint32_t)(int32_t)error);
    memcpy(r->data+20,request,16);
}
/* Parse the selectors without accepting unsupported NLAs.  Controller
 * discovery has exactly one native family; a foreign family name/ID must
 * reach ENOENT, never an arbitrary synthetic lookup result. */
static int family_selector(const uint8_t *bytes,unsigned size,bool dump) {
    bool has_id=false,has_name=false;
    uint16_t family_id=0;
    bool matched_name=false;
    unsigned off=0;
    while(off<size) {
        if(size-off<4)return -EINVAL;
        unsigned length=rd16(bytes+off);
        unsigned type=rd16(bytes+off+2)&NLA_TYPE_MASK;
        if(length<4 || length>size-off)return -EINVAL;
        const uint8_t *value=bytes+off+4;
        unsigned value_length=length-4;
        if(type==GENL_CTRL_FAMILY_ID) {
            if(has_id || value_length!=2)return -EINVAL;
            has_id=true;family_id=rd16(value);
        } else if(type==GENL_CTRL_FAMILY_NAME) {
            if(has_name || value_length<2 || value_length>16 ||
               value[value_length-1]!=0)return -EINVAL;
            has_name=true;
            for(unsigned i=0;i+1<value_length;i++)
                if(!value[i])return -EINVAL;
            matched_name=value_length==7 &&
                         !memcmp(value,"nlctrl",value_length);
        } else return -EOPNOTSUPP;
        off+=align4(length);
        if(off>size)return -EINVAL;
    }
    if(dump) {
        /* The kernel's controller dump enumerates registered families.
         * Reject a selective dump we cannot truthfully implement. */
        return has_id || has_name ? -EOPNOTSUPP : 0;
    }
    if(!has_id && !has_name)return -EINVAL;
    if((has_id && family_id!=NL_GENL_CTRL_ID) ||
       (has_name && !matched_name))return -ENOENT;
    return 0;
}
static int request_status(const uint8_t *message,unsigned length,
                          bool *respond,bool *dump) {
    const uint16_t type=rd16(message+4),flags=rd16(message+6);
    if(!(flags&NLM_F_REQUEST))return -EINVAL;
    if(type!=NL_GENL_CTRL_ID)return -EOPNOTSUPP;
    if(length<20 || rd16(message+18)!=0)return -EINVAL;
    if(message[16]!=GENL_CTRL_GETFAMILY)return -EOPNOTSUPP;
    if((flags&NLM_F_DUMP) && (flags&NLM_F_DUMP)!=NLM_F_DUMP)
        return -EOPNOTSUPP;
    *dump=(flags&NLM_F_DUMP)==NLM_F_DUMP;
    int rc=family_selector(message+20,length-20,*dump);
    if(rc)return rc;
    *respond=true;
    return 0;
}
int64_t nas_netlink_generic_sendto(unsigned slot,const uint8_t *bytes,
                                  unsigned length,const uint8_t *destination,
                                  unsigned destination_length) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes || length<16)return -EINVAL;
    if(length>NAS_NETLINK_GENERIC_MAX_DATAGRAM || length>s->send_quota)
        return -EMSGSIZE;
    uint32_t port=0,groups=0;
    int rc=0;
    if(destination)
        rc=parse_address(destination,destination_length,&port,&groups);
    else if(destination_length)rc=-EINVAL;
    if(rc)return rc;
    if(port || groups)return -EOPNOTSUPP;
    /* Full frame preflight and conservative queue reservation ensure that
     * one malformed skb cannot append partial controller responses. */
    unsigned records=0;
    for(unsigned pos=0;pos<length;) {
        if(length-pos<16)return -EINVAL;
        unsigned n=rd32(bytes+pos);
        if(n<16 || n>length-pos)return -EINVAL;
        unsigned aligned=align4(n);
        if(aligned>length-pos)return -EINVAL;
        bool reply=false,dump=false;
        int status=request_status(bytes+pos,n,&reply,&dump);
        records+=status?1U:1U+(dump?1U:0U)+
                 ((rd16(bytes+pos+6)&NLM_F_ACK)!=0);
        if(records>GENL_REPLY_LIMIT)return -EAGAIN;
        pos+=aligned;
    }
    if(!room(s,records))return -EAGAIN;
    if(!s->bound) {
        rc=autobind(s,0);
        if(rc)return rc;
    }
    for(unsigned pos=0;pos<length;) {
        const uint8_t *message=bytes+pos;
        unsigned n=rd32(message);
        bool respond=false,dump=false;
        rc=request_status(message,n,&respond,&dump);
        if(rc)ack(s,message,rc);
        else if(respond) {
            family_reply(s,rd32(message+8),dump);
            if(dump)done(s,rd32(message+8));
            if(rd16(message+6)&NLM_F_ACK)ack(s,message,0);
        }
        pos+=align4(n);
    }
    return length;
}
int64_t nas_netlink_generic_recvmsg(unsigned slot,uint8_t *destination,
                                   unsigned capacity,unsigned flags,
                                   uint8_t source[12],uint32_t *source_length,
                                   uint32_t *msg_flags) {
    struct genl_socket *s=live(slot);
    if(!s)return -EBADF;
    if(flags&~(MSG_PEEK|MSG_TRUNC|MSG_DONTWAIT))return -EOPNOTSUPP;
    if(capacity && !destination)return -EINVAL;
    if(!s->count)return -EAGAIN;
    struct genl_reply *r=&s->replies[s->head];
    const unsigned actual=r->length;
    const unsigned copy=actual<capacity?actual:capacity;
    if(copy)memcpy(destination,r->data,copy);
    if(source)make_address(source,0);
    if(source_length)*source_length=NAS_NETLINK_GENERIC_SOCKADDR_SIZE;
    if(msg_flags)*msg_flags=copy<actual?MSG_TRUNC:0;
    if(!(flags&MSG_PEEK)) {
        s->head=(uint8_t)((s->head+1U)%GENL_REPLY_LIMIT);
        s->count--;
    }
    return flags&MSG_TRUNC?actual:copy;
}
static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct genl_socket *s=live(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    if(!s->count)return (uint64_t)-EAGAIN;
    const unsigned length=s->replies[s->head].length;
    return requested<length?requested:length;
}
static uint64_t read_bytes(unsigned slot,uint8_t *out,uint64_t count) {
    return (uint64_t)nas_netlink_generic_recvmsg(slot,out,(unsigned)count,
                                                 0,NULL,NULL,NULL);
}
static uint16_t poll_description(unsigned slot,uint16_t events) {
    struct genl_socket *s=live(slot);
    if(!s)return PORT_POLLERR;
    uint16_t ready=0;
    if(s->count)ready|=PORT_POLLIN;
    if(room(s,1))ready|=PORT_POLLOUT;
    return (uint16_t)(ready&events);
}
static uint16_t poll_fd(uint64_t fd,uint16_t events) {
    if(!nas_current_task || !nas_current_task->fds || fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_SOCKET)return 0;
    return poll_description(nas_current_task->fds->entries[fd].slot,events);
}
static void release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)
        sockets[slot]=(struct genl_socket){0};
}
static const struct nas_file_ops ops={
    .read_plan=read_plan,.read=read_bytes,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=poll_fd,.poll_description=poll_description,.release=release
};
const struct nas_file_ops *nas_netlink_generic_ops(void) {return &ops;}
