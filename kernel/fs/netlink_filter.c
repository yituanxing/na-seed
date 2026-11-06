/* Bounded Linux NETLINK_NETFILTER/nf_tables control plane. This is a real
 * table registry with per-socket atomic NFNL batches, not a packet-filter
 * implementation. The only mutable objects supported are empty tables;
 * chains/rules/sets and foreign nfnetlink subsystems return explicit errors.
 * The socket adapter owns all userspace copies and FD lifetime. */
#include "netlink_filter.h"
#include "file_ops.h"
#include "../fd.h"
#include "../task.h"
#include "../task/capability.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../drivers/serial.h"

#define NLMSG_NOOP 1U
#define NLMSG_ERROR 2U
#define NLMSG_DONE 3U
#define NFNL_BATCH_BEGIN 16U
#define NFNL_BATCH_END 17U
#define NLM_F_REQUEST 0x0001U
#define NLM_F_MULTI 0x0002U
#define NLM_F_ACK 0x0004U
#define NLM_F_ECHO 0x0008U
#define NLM_F_DUMP 0x0300U
#define NLM_F_REPLACE 0x0100U
#define NLM_F_EXCL 0x0200U
#define NLM_F_CREATE 0x0400U
#define NFNL_NFTABLES 10U
#define NFT_NEWTABLE 0U
#define NFT_GETTABLE 1U
#define NFT_DELTABLE 2U
#define NFT_GETCHAIN 4U
#define NFT_GETRULE 7U
#define NFT_GETSET 10U
#define NFT_GETSETELEM 13U
#define NFT_NEWGEN 15U
#define NFT_GETGEN 16U
#define NFT_GETOBJ 19U
#define NFT_GETFLOWTABLE 23U
#define NFATTR_TABLE_NAME 1U
#define NFATTR_TABLE_FLAGS 2U
#define NFATTR_TABLE_USE 3U
#define NFATTR_TABLE_HANDLE 4U
#define NFATTR_GEN_ID 1U
#define NFATTR_BATCH_GENID 1U
#define NFPROTO_UNSPEC 0U
#define NFPROTO_INET 1U
#define NFPROTO_IPV4 2U
#define NFPROTO_ARP 3U
#define NFPROTO_NETDEV 5U
#define NFPROTO_BRIDGE 7U
#define NFPROTO_IPV6 10U
#define AF_NETLINK 16U
#define MSG_PEEK 2U
#define MSG_TRUNC 32U
#define MSG_DONTWAIT 64U
#define PORT_POLLIN 1U
#define PORT_POLLOUT 4U
#define PORT_POLLERR 8U
#define NF_TABLE_LIMIT 16U
#define NF_TABLE_NAME_BYTES 256U
#define NF_REPLY_BYTES 320U
#define NF_REPLY_LIMIT 18U
#define NF_BATCH_OPS 16U
#define NF_SNDBUF_MIN 2048U
#define NF_SNDBUF_MAX 262144U

struct nf_table {
    uint8_t family;
    uint64_t handle;
    char name[NF_TABLE_NAME_BYTES];
};
struct nf_reply { uint16_t len; uint8_t data[NF_REPLY_BYTES]; };
struct nf_batch_operation {
    uint8_t original_header[16];
    int error;
    bool ack;
};
struct nf_batch {
    bool active,failed,changed;
    uint32_t starting_generation;
    uint64_t next_table_handle;
    unsigned count;
    struct nf_table tables[NF_TABLE_LIMIT];
    unsigned table_count;
    struct nf_batch_operation operations[NF_BATCH_OPS];
};
struct nf_socket {
    bool owned,bound;
    uint32_t owner_pid,port_id,send_quota,recv_quota;
    uint8_t head,count;
    struct nf_reply replies[NF_REPLY_LIMIT];
};
static struct nf_socket sockets[NAS_OPEN_FILE_LIMIT];
/* nfnetlink batches never span skb/sendmsg. NaSeed currently executes one
 * non-preemptible syscall in its kernel at a time, so this scratch storage
 * is reused per call; storing an entire 4-KiB staging snapshot per possible
 * FD slot would waste over half a MiB of permanently reserved kernel RAM. */
static struct nf_batch batch_scratch;
static struct nf_table global_tables[NF_TABLE_LIMIT];
static unsigned global_table_count;
static uint32_t global_generation=1;
static uint64_t global_next_table_handle=1;
static uint32_t next_port_id=UINT32_C(0x50000000);
/* Host fixtures prove the table state machine, but the original Alpine nft
 * may emit a different sequence (notably during `nft -c -f`).  A bounded
 * wire-level rejection record identifies the exact type/flags/first NLA
 * on genuine QEMU without dumping kernel or userspace memory. */
static unsigned rejected_noent_traces;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|p[3];
}
static void wr16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static void wb32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static void wb64(uint8_t *p,uint64_t v) {
    wb32(p,(uint32_t)(v>>32));
    wb32(p+4,(uint32_t)v);
}
static unsigned align4(unsigned n) {return (n+3U)&~3U;}
static bool valid_family(uint8_t family) {
    return family==NFPROTO_INET || family==NFPROTO_IPV4 ||
           family==NFPROTO_IPV6 || family==NFPROTO_ARP ||
           family==NFPROTO_BRIDGE || family==NFPROTO_NETDEV;
}
static struct nf_socket *live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].owned &&
           nas_open_files[slot].refcount ? &sockets[slot]:NULL;
}
bool nas_netlink_filter_is_slot(unsigned slot) {return live(slot)!=NULL;}
int nas_netlink_filter_open(unsigned slot,uint32_t owner_pid) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return -EBADF;
    if(sockets[slot].owned)return -EBUSY;
    sockets[slot]=(struct nf_socket){.owned=true,.owner_pid=owner_pid,
        .send_quota=NAS_NETLINK_FILTER_MAX_DATAGRAM,
        .recv_quota=NF_REPLY_LIMIT*NF_REPLY_BYTES};
    return 0;
}
int nas_netlink_filter_protocol(unsigned slot) {
    return live(slot) ? (int)NAS_NETLINK_FILTER_PROTOCOL:-EBADF;
}
static bool port_used(uint32_t pid) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].owned && sockets[i].bound &&
           sockets[i].port_id==pid)return true;
    return false;
}
static int autobind(struct nf_socket *s,uint32_t requested) {
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
static int parse_sockaddr(const uint8_t *a,unsigned length,
                          uint32_t *port,uint32_t *groups) {
    if(!a || length<NAS_NETLINK_FILTER_SOCKADDR_SIZE)return -EINVAL;
    if(rd16(a)!=AF_NETLINK)return -EAFNOSUPPORT;
    if(rd16(a+2))return -EINVAL;
    *port=rd32(a+4);*groups=rd32(a+8);
    return 0;
}
static void make_sockaddr(uint8_t *a,uint32_t port) {
    memset(a,0,NAS_NETLINK_FILTER_SOCKADDR_SIZE);
    wr16(a,AF_NETLINK);wr32(a+4,port);
}
int nas_netlink_filter_bind(unsigned slot,const uint8_t *address,unsigned length) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    uint32_t port,groups;
    int rc=parse_sockaddr(address,length,&port,&groups);
    if(rc)return rc;
    /* Multicast events are not delivered, so subscribing cannot succeed. */
    if(groups)return -EOPNOTSUPP;
    return autobind(s,port);
}
int nas_netlink_filter_getsockname(unsigned slot,uint8_t address[12]) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!address)return -EINVAL;
    make_sockaddr(address,s->bound?s->port_id:0);
    return 0;
}
int nas_netlink_filter_set_sndbuf(unsigned slot,uint32_t requested) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    uint64_t amount=(uint64_t)requested*2U;
    if(amount<NF_SNDBUF_MIN)amount=NF_SNDBUF_MIN;
    if(amount>NF_SNDBUF_MAX)amount=NF_SNDBUF_MAX;
    s->send_quota=(uint32_t)amount;
    return 0;
}
int nas_netlink_filter_get_sndbuf(unsigned slot,uint32_t *bytes) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->send_quota;
    return 0;
}
int nas_netlink_filter_set_rcvbuf(unsigned slot,uint32_t requested) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    uint64_t amount=(uint64_t)requested*2U;
    if(amount<NF_REPLY_BYTES)amount=NF_REPLY_BYTES;
    if(amount>NF_REPLY_LIMIT*NF_REPLY_BYTES)
        amount=NF_REPLY_LIMIT*NF_REPLY_BYTES;
    s->recv_quota=(uint32_t)amount;
    return 0;
}
int nas_netlink_filter_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes)return -EINVAL;
    *bytes=s->recv_quota;
    return 0;
}

static bool room(const struct nf_socket *s,unsigned needed) {
    return (unsigned)s->count+needed<=NF_REPLY_LIMIT &&
           ((uint64_t)s->count+needed)*NF_REPLY_BYTES<=s->recv_quota;
}
static struct nf_reply *new_reply(struct nf_socket *s) {
    struct nf_reply *r=&s->replies[(s->head+s->count)%NF_REPLY_LIMIT];
    memset(r,0,sizeof(*r));s->count++;
    return r;
}
static struct nf_reply *header(struct nf_socket *s,unsigned bytes,
                                uint16_t type,uint16_t flags,uint32_t seq) {
    struct nf_reply *r=new_reply(s);
    r->len=(uint16_t)bytes;
    wr32(r->data,bytes);wr16(r->data+4,type);wr16(r->data+6,flags);
    /* Linux nft reply nlmsghdr.nlmsg_pid addresses the requesting socket;
     * the enclosing recvmsg sockaddr_nl remains the kernel's port zero. */
    wr32(r->data+8,seq);wr32(r->data+12,s->port_id);
    return r;
}
static void ack(struct nf_socket *s,const uint8_t original[16],int error) {
    struct nf_reply *r=header(s,36,NLMSG_ERROR,0,rd32(original+8));
    wr32(r->data+16,(uint32_t)(int32_t)error);
    memcpy(r->data+20,original,16); /* struct nlmsgerr.msg */
}
static void done(struct nf_socket *s,uint32_t seq) {
    struct nf_reply *r=header(s,20,NLMSG_DONE,NLM_F_MULTI,seq);
    wr32(r->data+16,0);
}
static void gen_reply(struct nf_socket *s,uint32_t seq) {
    /* Linux GETGEN's reply is NEWGEN, with the generation in both the
     * nfgenmsg.res_id low bits (BE16) and NFTA_GEN_ID (BE32). */
    struct nf_reply *r=header(s,28,(NFNL_NFTABLES<<8)|NFT_NEWGEN,0,seq);
    r->data[16]=NFPROTO_UNSPEC;
    r->data[18]=(uint8_t)(global_generation>>8);
    r->data[19]=(uint8_t)global_generation;
    wr16(r->data+20,8);wr16(r->data+22,NFATTR_GEN_ID);
    wb32(r->data+24,global_generation);
}
static void table_reply(struct nf_socket *s,uint32_t seq,
                        const struct nf_table *t,bool multi,uint16_t op) {
    const unsigned name_bytes=(unsigned)strlen(t->name)+1U;
    const unsigned n=align4(4U+name_bytes);
    const unsigned total=20U+n+28U;
    struct nf_reply *r=header(s,total,(NFNL_NFTABLES<<8)|op,
                               multi ? NLM_F_MULTI:0,seq);
    r->data[16]=t->family;
    /* nf_tables_fill_table_info: nfgenmsg.res_id is the BE16 low part of
     * the same live generation returned by NFT_MSG_NEWGEN. */
    r->data[18]=(uint8_t)(global_generation>>8);
    r->data[19]=(uint8_t)global_generation;
    wr16(r->data+20,(uint16_t)(4U+name_bytes));
    wr16(r->data+22,NFATTR_TABLE_NAME);
    memcpy(r->data+24,t->name,name_bytes);
    const unsigned off=20U+n;
    /* Linux nf_tables_fill_table_info emits NAME, USE, HANDLE, FLAGS in
     * that order.  A stable nonzero handle belongs to each registered
     * table and is not invented separately when userspace reads it. */
    wr16(r->data+off,8);wr16(r->data+off+2,NFATTR_TABLE_USE);
    wb32(r->data+off+4,0); /* zero chains, genuinely no references */
    wr16(r->data+off+8,12);wr16(r->data+off+10,NFATTR_TABLE_HANDLE);
    wb64(r->data+off+12,t->handle);
    wr16(r->data+off+20,8);wr16(r->data+off+22,NFATTR_TABLE_FLAGS);
    wb32(r->data+off+24,0); /* zero flags: no table hooks configured */
}
static int locate(const struct nf_table *tables,unsigned count,
                  uint8_t family,const char *name) {
    for(unsigned i=0;i<count;i++)
        if(tables[i].family==family && !strcmp(tables[i].name,name))
            return (int)i;
    return -1;
}

struct nf_attrs {char name[NF_TABLE_NAME_BYTES];bool has_name;};
static int read_table_attrs(const uint8_t *data,unsigned size,
                            bool modifying,struct nf_attrs *result) {
    *result=(struct nf_attrs){0};
    unsigned off=0;
    while(off<size) {
        if(size-off<4)return -EINVAL;
        unsigned length=rd16(data+off);
        unsigned type=rd16(data+off+2)&0x3fffU;
        if(length<4 || length>size-off)return -EINVAL;
        if(type==NFATTR_TABLE_NAME) {
            unsigned text_len=length-4U;
            if(result->has_name || text_len<2 ||
               text_len>NF_TABLE_NAME_BYTES)return -EINVAL;
            const uint8_t *str=data+off+4;
            if(str[text_len-1]!=0)return -EINVAL;
            for(unsigned i=0;i<text_len-1;i++)
                if(!str[i])return -EINVAL;
            memcpy(result->name,str,text_len);
            result->has_name=true;
        } else if(type==NFATTR_TABLE_FLAGS && modifying) {
            if(length!=8)return -EINVAL;
            /* Dormant, owner and persistence all require semantics this
             * bounded model cannot yet provide; do not silently ignore. */
            if(be32(data+off+4))return -EOPNOTSUPP;
        } else return -EOPNOTSUPP;
        off+=align4(length);
        if(off>size)return -EINVAL;
    }
    return 0;
}
static int read_batch_genid(const uint8_t *data,unsigned size,
                            uint32_t *expected) {
    *expected=0;
    unsigned off=0;
    while(off<size) {
        if(size-off<4)return -EINVAL;
        unsigned length=rd16(data+off);
        unsigned type=rd16(data+off+2)&0x3fffU;
        if(length<4 || length>size-off)return -EINVAL;
        if(type!=NFATTR_BATCH_GENID || length!=8)return -EINVAL;
        if(*expected)return -EINVAL;
        *expected=be32(data+off+4);
        off+=align4(length);
        if(off>size)return -EINVAL;
    }
    return 0;
}
/* An empty ruleset still accepts real nf_tables filtered dumps.  In
 * particular, libnftnl sends NFTA_SET_TABLE for `nft list table ...`; a
 * filtered request is not an unsupported operation merely because it has
 * attributes after nfgenmsg.  Validate the complete NLA stream before
 * returning an honest empty NLMSG_DONE; no chain/rule/set is fabricated. */
static int read_empty_dump_attrs(const uint8_t *data,unsigned size,
                                 uint16_t operation) {
    /* These are the distinct UAPI attribute IDs for each operation:
     * CHAIN TABLE=1 NAME=3; RULE TABLE=1 CHAIN=2;
     * SET TABLE=1; SETELEM TABLE=1 SET=2;
     * OBJ TABLE=1 TYPE=3; FLOWTABLE TABLE=1 NAME=2. */
    unsigned second=0,numeric=0;
    switch(operation) {
    case NFT_GETCHAIN:second=3;break;
    case NFT_GETRULE:second=2;break;
    case NFT_GETSET:break;
    case NFT_GETSETELEM:second=2;break;
    case NFT_GETOBJ:numeric=3;break;
    case NFT_GETFLOWTABLE:second=2;break;
    default:return -EOPNOTSUPP;
    }
    bool seen_table=false,seen_second=false,seen_number=false;
    unsigned off=0;
    while(off<size) {
        if(size-off<4)return -EINVAL;
        const unsigned length=rd16(data+off);
        const unsigned type=rd16(data+off+2)&0x3fffU;
        if(length<4 || length>size-off)return -EINVAL;
        if(type==1U || (second && type==second)) {
            bool *seen=type==1U?&seen_table:&seen_second;
            const unsigned text_len=length-4U;
            if(*seen || text_len<2U || text_len>NF_TABLE_NAME_BYTES)
                return -EINVAL;
            const uint8_t *name=data+off+4;
            if(name[text_len-1U])return -EINVAL;
            for(unsigned i=0;i<text_len-1U;i++)
                if(!name[i])return -EINVAL;
            *seen=true;
        } else if(numeric && type==numeric) {
            /* NFTA_OBJ_TYPE is a network-order U32.  Unknown selectors
             * must not be silently treated as an empty successful dump. */
            if(seen_number || length!=8U)return -EINVAL;
            if(be32(data+off+4)>10U)return -EOPNOTSUPP;
            seen_number=true;
        } else return -EOPNOTSUPP;
        off+=align4(length);
        if(off>size)return -EINVAL;
    }
    return 0;
}
static int table_update(struct nf_table *tables,unsigned *count,
                        uint8_t family,const char *name,uint16_t operation,
                        uint16_t flags,uint64_t *next_handle,bool *changed) {
    int index=locate(tables,*count,family,name);
    if(operation==NFT_NEWTABLE) {
        if(index>=0) {
            if(flags&NLM_F_EXCL)return -EEXIST;
            if(flags&NLM_F_REPLACE)return -EOPNOTSUPP;
            /* idempotent create with no requested change */
            return 0;
        }
        /* Linux nf_tables_newtable permits an unflagged NFT_NEWTABLE to
         * create a missing table. This is not hypothetical: official Alpine
         * nft's `nft -c -f` emits exactly NLM_F_REQUEST (without CREATE or
         * ACK), staging the table in a BEGIN-without-END dry-run batch.
         * Requiring CREATE here wrongly returned ENOENT before the parser
         * check could finish. EXCL still rejects existing tables above. */
        if(*count==NF_TABLE_LIMIT)return -ENOSPC;
        if(!*next_handle)return -ENOSPC;
        struct nf_table *t=&tables[(*count)++];
        memset(t,0,sizeof(*t));t->family=family;
        t->handle=(*next_handle)++;
        memcpy(t->name,name,strlen(name)+1U);
        *changed=true;
        return 0;
    }
    if(index<0)return -ENOENT;
    for(unsigned j=(unsigned)index+1U;j<*count;j++)
        tables[j-1]=tables[j];
    (*count)--;
    *changed=true;
    return 0;
}

static bool admin_allowed(void) {
    return nas_current_task &&
       (nas_cap_effective(&nas_current_task->credentials)&
        NAS_CAP_NET_ADMIN)!=0;
}
static void trace_noent(const uint8_t *message,bool in_batch) {
    if(rejected_noent_traces>=8U)return;
    rejected_noent_traces++;
    const uint32_t n=rd32(message);
    nas_print("[NFT ENOENT] type=");nas_hex(rd16(message+4));
    nas_print(" flags=");nas_hex(rd16(message+6));
    nas_print(" family=");nas_dec(n>=20U?message[16]:255U);
    nas_print(" batch=");nas_dec(in_batch?1U:0U);
    nas_print(" length=");nas_dec(n);
    nas_print(" attr=");nas_dec(n>=24U?rd16(message+22)&0x3fffU:0U);
    nas_print(" name=");
    if(n>=26U && rd16(message+20)>=6U &&
       rd16(message+20)<=n-20U && rd16(message+22)==NFATTR_TABLE_NAME) {
        const unsigned limit=rd16(message+20)-4U;
        for(unsigned i=0;i<limit && i<32U && message[24+i];i++) {
            const uint8_t c=message[24+i];
            nas_putchar(c>=32U && c<127U?(char)c:'?');
        }
    }
    nas_print(" gen=");nas_dec(global_generation);
    nas_putchar('\n');
}
static bool batch_subsystem(const uint8_t *message) {
    /* libnftnl's older batch helper encoded res_id in host order, while
     * current UAPI labels it __be16. Linux nfnetlink_rcv has a dedicated
     * compatibility branch for this. Both encodings identify nftables. */
    return (message[18]==0 && message[19]==NFNL_NFTABLES) ||
           (message[18]==NFNL_NFTABLES && message[19]==0);
}
static int handle_nft(struct nf_socket *s,const uint8_t *message,
                      uint32_t length,uint16_t type,uint16_t flags) {
    const uint16_t operation=(uint16_t)(type&255U);
    const uint32_t seq=rd32(message+8);
    struct nf_batch *batch=&batch_scratch;
    if((type>>8)!=NFNL_NFTABLES)return -EOPNOTSUPP;
    if(length<20 || message[17]!=0 || rd16(message+18)!=0)return -EINVAL;
    const uint8_t family=message[16];
    if(operation==NFT_GETGEN) {
        if(length!=20 || family!=NFPROTO_UNSPEC)return -EINVAL;
        if(batch->active)return -EOPNOTSUPP;
        if(!room(s,1U+((flags&NLM_F_ACK)!=0)))return -EAGAIN;
        gen_reply(s,seq);
        if(flags&NLM_F_ACK)ack(s,message,0);
        return 0;
    }
    if(!valid_family(family) &&
       !(family==NFPROTO_UNSPEC && (flags&NLM_F_DUMP)))return -EAFNOSUPPORT;
    const bool dump=(flags&NLM_F_DUMP)==NLM_F_DUMP;
    if(operation==NFT_GETCHAIN || operation==NFT_GETRULE ||
       operation==NFT_GETSET || operation==NFT_GETSETELEM ||
       operation==NFT_GETOBJ || operation==NFT_GETFLOWTABLE) {
        /* No configured chains/rules/sets/objects/flowtables can exist while
         * their creation requests are rejected. Empty dump is therefore an
         * accurate registry result, not a fabricated success payload. */
        if(batch->active || !dump)return -EOPNOTSUPP;
        int rc=read_empty_dump_attrs(message+20,length-20,operation);
        if(rc)return rc;
        if(!room(s,1U+((flags&NLM_F_ACK)!=0)))return -EAGAIN;
        done(s,seq);
        if(flags&NLM_F_ACK)ack(s,message,0);
        return 0;
    }
    if(operation!=NFT_GETTABLE && operation!=NFT_NEWTABLE &&
       operation!=NFT_DELTABLE)return -EOPNOTSUPP;
    struct nf_attrs attrs;
    int rc=read_table_attrs(message+20,length-20,
                            operation!=NFT_GETTABLE,&attrs);
    if(rc)return rc;
    if(operation==NFT_GETTABLE) {
        if(batch->active)return -EOPNOTSUPP;
        if(dump) {
            unsigned count=0;
            for(unsigned i=0;i<global_table_count;i++)
                if((family==NFPROTO_UNSPEC ||
                    global_tables[i].family==family) &&
                   (!attrs.has_name ||
                    !strcmp(global_tables[i].name,attrs.name)))count++;
            if(!room(s,count+1U+((flags&NLM_F_ACK)!=0)))return -EAGAIN;
            for(unsigned i=0;i<global_table_count;i++)
                if((family==NFPROTO_UNSPEC ||
                    global_tables[i].family==family) &&
                   (!attrs.has_name ||
                    !strcmp(global_tables[i].name,attrs.name)))
                    table_reply(s,seq,&global_tables[i],true,NFT_NEWTABLE);
            done(s,seq);
        } else {
            if(!attrs.has_name)return -EINVAL;
            int index=locate(global_tables,global_table_count,family,attrs.name);
            if(index<0)return -ENOENT;
            if(!room(s,1U+((flags&NLM_F_ACK)!=0)))return -EAGAIN;
            table_reply(s,seq,&global_tables[index],false,NFT_NEWTABLE);
        }
        if(flags&NLM_F_ACK)ack(s,message,0);
        return 0;
    }
    if(!attrs.has_name || dump)return -EINVAL;
    if((flags&NLM_F_REPLACE) ||
       (batch->active && (flags&NLM_F_ECHO)))return -EOPNOTSUPP;
    if(batch->active) {
        if(batch->count==NF_BATCH_OPS)return -ENOSPC;
        struct nf_batch_operation *op=&batch->operations[batch->count++];
        memcpy(op->original_header,message,16);
        op->ack=(flags&NLM_F_ACK)!=0;op->error=0;
        op->error=table_update(batch->tables,&batch->table_count,family,
                               attrs.name,operation,flags,
                               &batch->next_table_handle,&batch->changed);
        if(op->error)batch->failed=true;
        return 0;
    }
    if(!room(s,1U+((flags&NLM_F_ECHO)!=0)))return -EAGAIN;
    struct nf_table echo_table={.family=family};
    if((flags&NLM_F_ECHO) && operation==NFT_DELTABLE) {
        int index=locate(global_tables,global_table_count,family,attrs.name);
        if(index>=0)echo_table=global_tables[index];
    }
    bool changed=false;
    rc=table_update(global_tables,&global_table_count,family,attrs.name,
                    operation,flags,&global_next_table_handle,&changed);
    if(rc)return rc;
    if(changed)global_generation++;
    if(flags&NLM_F_ECHO) {
        if(operation==NFT_NEWTABLE) {
            int index=locate(global_tables,global_table_count,family,attrs.name);
            if(index>=0)echo_table=global_tables[index];
        }
        table_reply(s,seq,&echo_table,false,operation);
    }
    if(flags&NLM_F_ACK)ack(s,message,0);
    return 0;
}

/* Linux nfnetlink processes a batch within one skb/sendmsg. `nft -c` sends
 * BEGIN+operations with NO END: validate, emit requested ACKs, then abort
 * without mutation. Only an END in the same datagram commits the registry.
 * This is distinct from claiming success for an operation we did not parse. */
static int finish_batch(struct nf_socket *s,bool commit,
                        const uint8_t *end_header) {
    struct nf_batch *b=&batch_scratch;
    unsigned responses=end_header && (rd16(end_header+6)&NLM_F_ACK) ? 1U:0U;
    for(unsigned i=0;i<b->count;i++)
        if(b->operations[i].ack || b->operations[i].error ||
           b->failed)responses++;
    if(!room(s,responses))return -EAGAIN;
    if(commit && !b->failed && global_generation!=b->starting_generation) {
        b->failed=true;
        for(unsigned i=0;i<b->count;i++)
            if(!b->operations[i].error)
                b->operations[i].error=-EAGAIN;
    }
    if(commit && !b->failed && b->changed) {
        global_table_count=b->table_count;
        memcpy(global_tables,b->tables,
               b->table_count*sizeof(*global_tables));
        global_next_table_handle=b->next_table_handle;
        global_generation++;
    }
    for(unsigned i=0;i<b->count;i++) {
        struct nf_batch_operation *op=&b->operations[i];
        if(op->ack || op->error)
            ack(s,op->original_header,op->error);
    }
    if(end_header && (rd16(end_header+6)&NLM_F_ACK))
        ack(s,end_header,b->failed?-ECANCELED:0);
    memset(b,0,sizeof(*b));
    return 0;
}

int64_t nas_netlink_filter_sendto(unsigned slot,const uint8_t *bytes,
                                 unsigned length,const uint8_t *destination,
                                 unsigned destination_length) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!bytes || length<16)return -EINVAL;
    if(length>NAS_NETLINK_FILTER_MAX_DATAGRAM || length>s->send_quota)
        return -EMSGSIZE;
    uint32_t target=0,groups=0;
    int rc=0;
    if(destination)
        rc=parse_sockaddr(destination,destination_length,&target,&groups);
    else if(destination_length)rc=-EINVAL;
    if(rc)return rc;
    if(target || groups)return -EOPNOTSUPP;
    /* Reject malformed boundaries before the first state mutation. */
    unsigned count=0;
    for(unsigned pos=0;pos<length;) {
        if(length-pos<16)return -EINVAL;
        unsigned n=rd32(bytes+pos);
        if(n<16 || n>length-pos)return -EINVAL;
        unsigned step=align4(n);
        if(step>length-pos)return -EINVAL;
        pos+=step;
        if(++count>NF_REPLY_LIMIT)return -EMSGSIZE;
    }
    if(!admin_allowed()) {
        if(!room(s,count))return -EAGAIN;
        for(unsigned pos=0;pos<length;) {
            const unsigned n=rd32(bytes+pos);
            ack(s,bytes+pos,-EPERM);
            pos+=align4(n);
        }
        return length;
    }
    if(!s->bound) {
        rc=autobind(s,0);
        if(rc)return rc;
    }
    memset(&batch_scratch,0,sizeof(batch_scratch));
    for(unsigned pos=0;pos<length;) {
        const uint8_t *msg=bytes+pos;
        const unsigned n=rd32(msg);
        const uint16_t type=rd16(msg+4),flags=rd16(msg+6);
        pos+=align4(n);
        if(!(flags&NLM_F_REQUEST))rc=-EINVAL;
        else if(type==NFNL_BATCH_BEGIN) {
            if(batch_scratch.active || n<20 || msg[17] || !batch_subsystem(msg))
                rc=-EINVAL;
            else {
                uint32_t expected=0;
                rc=read_batch_genid(msg+20,n-20,&expected);
                if(!rc && expected && expected!=global_generation)
                    rc=-EAGAIN;
                if(!rc) {
                    struct nf_batch *b=&batch_scratch;
                    memset(b,0,sizeof(*b));b->active=true;
                    b->starting_generation=global_generation;
                    b->next_table_handle=global_next_table_handle;
                    b->table_count=global_table_count;
                    memcpy(b->tables,global_tables,
                           global_table_count*sizeof(*global_tables));
                }
            }
            if(!rc && (flags&NLM_F_ACK) && room(s,1))ack(s,msg,0);
        } else if(type==NFNL_BATCH_END) {
            struct nf_batch *b=&batch_scratch;
            if(!b->active || n!=20 || msg[17] || !batch_subsystem(msg))
                rc=-EINVAL;
            else rc=finish_batch(s,true,msg);
        } else if(type==NLMSG_NOOP)rc=0;
        else rc=handle_nft(s,msg,n,type,flags);
        if(rc) {
            if(rc==-ENOENT)trace_noent(msg,batch_scratch.active);
            if(batch_scratch.active && batch_scratch.count<NF_BATCH_OPS) {
                /* Linux nfnetlink collects both successful ACK requests
                 * and errors, then emits them in request order after the
                 * whole batch has been committed or aborted. */
                struct nf_batch_operation *op=
                    &batch_scratch.operations[batch_scratch.count++];
                memcpy(op->original_header,msg,16);
                op->ack=true;op->error=rc;
                batch_scratch.failed=true;
            } else {
                if(batch_scratch.active)batch_scratch.failed=true;
                if(!room(s,1))return -EAGAIN;
                ack(s,msg,rc);
            }
            rc=0;
        }
    }
    if(batch_scratch.active) {
        /* No END means a real dry run, not an abandoned staged mutation.
         * Preserve errors and ACK valid operations after verification. */
        rc=finish_batch(s,false,NULL);
        if(rc)return rc;
    }
    return length;
}

int64_t nas_netlink_filter_recvmsg(unsigned slot,uint8_t *destination,
                                  unsigned capacity,unsigned flags,
                                  uint8_t source[12],uint32_t *source_length,
                                  uint32_t *msg_flags) {
    struct nf_socket *s=live(slot);
    if(!s)return -EBADF;
    if(flags&~(MSG_PEEK|MSG_TRUNC|MSG_DONTWAIT))return -EOPNOTSUPP;
    if(capacity && !destination)return -EINVAL;
    if(!s->count)return -EAGAIN;
    struct nf_reply *r=&s->replies[s->head];
    unsigned actual=r->len,copy=capacity<actual?capacity:actual;
    if(copy)memcpy(destination,r->data,copy);
    if(source)make_sockaddr(source,0);
    if(source_length)*source_length=NAS_NETLINK_FILTER_SOCKADDR_SIZE;
    if(msg_flags)*msg_flags=copy<actual?MSG_TRUNC:0;
    if(!(flags&MSG_PEEK)) {
        s->head=(uint8_t)((s->head+1U)%NF_REPLY_LIMIT);
        s->count--;
    }
    return (flags&MSG_TRUNC)?actual:copy;
}
static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct nf_socket *s=live(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    if(!s->count)return (uint64_t)-EAGAIN;
    return requested<s->replies[s->head].len ? requested:
           s->replies[s->head].len;
}
static uint64_t read_bytes(unsigned slot,uint8_t *out,uint64_t count) {
    return (uint64_t)nas_netlink_filter_recvmsg(slot,out,(unsigned)count,
                                                0,NULL,NULL,NULL);
}
static uint16_t poll_description(unsigned slot,uint16_t events) {
    struct nf_socket *s=live(slot);
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
        sockets[slot]=(struct nf_socket){0};
}
static const struct nas_file_ops ops={
    .read_plan=read_plan,.read=read_bytes,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=poll_fd,.poll_description=poll_description,.release=release
};
const struct nas_file_ops *nas_netlink_filter_ops(void){return &ops;}
