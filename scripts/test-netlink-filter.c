/* Host test of production NFNETLINK/nf_tables kernel-buffer transport.
 * Wire packets follow Linux netlink.h, nfnetlink.h and nf_tables.h; no nft
 * executable, simulated userspace success, or special test-only kernel path. */
#include "../kernel/fs/netlink_filter.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/task/capability.h"
#include "../kernel/errno.h"
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task;
static struct nas_task task;
static uint32_t sequence=10;

/* Host wire fixtures have no serial device; production QEMU keeps the
 * bounded rejection trace enabled for original Alpine userspace packets. */
void nas_print(const char *text) {(void)text;}
void nas_putchar(char ch) {(void)ch;}
void nas_dec(uint64_t number) {(void)number;}
void nas_hex(uint64_t number) {(void)number;}

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
static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p)<<32)|be32(p+4);
}
static void wr16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void wr32(uint8_t *p,uint32_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
    p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);
}
static void wb32(uint8_t *p,uint32_t n) {
    p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);
    p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;
}
static unsigned align4(unsigned n) {return (n+3U)&~3U;}
static unsigned make_msg(uint8_t *out,uint16_t type,uint16_t flags,
                         uint8_t family) {
    memset(out,0,512);
    wr32(out,20);wr16(out+4,type);wr16(out+6,flags|NLM_F_REQUEST);
    wr32(out+8,++sequence);out[16]=family;
    return 20;
}
static unsigned add_string_attr(uint8_t *msg,unsigned length,
                                uint16_t type,const char *name) {
    unsigned n=(unsigned)strlen(name)+1U,attr=4U+n;
    assert(length+align4(attr)<=512);
    wr16(msg+length,(uint16_t)attr);
    wr16(msg+length+2,type);
    memcpy(msg+length+4,name,n);
    length+=align4(attr);
    wr32(msg,length);
    return length;
}
static unsigned add_name(uint8_t *msg,unsigned length,const char *name) {
    return add_string_attr(msg,length,NFTA_TABLE_NAME,name);
}
static unsigned add_obj_type(uint8_t *msg,unsigned length,uint32_t type) {
    assert(length+8<=512);
    wr16(msg+length,8);wr16(msg+length+2,NFTA_OBJ_TYPE);
    wb32(msg+length+4,type);
    wr32(msg,length+8);
    return length+8;
}
static unsigned add_genid(uint8_t *msg,unsigned length,uint32_t genid) {
    assert(length+8<=512);
    wr16(msg+length,8);wr16(msg+length+2,NFNL_BATCH_GENID);
    wb32(msg+length+4,genid);
    wr32(msg,length+8);
    return length+8;
}
static unsigned add_table_flags(uint8_t *msg,unsigned length,uint32_t flags) {
    assert(length+8<=512);
    wr16(msg+length,8);wr16(msg+length+2,NFTA_TABLE_FLAGS);
    wb32(msg+length+4,flags);
    wr32(msg,length+8);
    return length+8;
}
static void send_one(unsigned slot,const uint8_t *req,unsigned bytes) {
    assert(nas_netlink_filter_sendto(slot,req,bytes,NULL,0)==(int64_t)bytes);
}
static unsigned pop(unsigned slot,uint8_t *out) {
    int64_t n=nas_netlink_filter_recvmsg(slot,out,512,0,NULL,NULL,NULL);
    assert(n>=16 && n<=320);
    assert(rd32(out)==(uint32_t)n);
    uint8_t local[12];
    assert(nas_netlink_filter_getsockname(slot,local)==0);
    assert(rd32(out+12)==rd32(local+4));
    return (unsigned)n;
}
static void empty(unsigned slot) {
    uint8_t out[512];
    assert(nas_netlink_filter_recvmsg(slot,out,sizeof(out),MSG_DONTWAIT,
                                      NULL,NULL,NULL)==-EAGAIN);
}
static void ack_error(unsigned slot,uint32_t seq,int error) {
    uint8_t out[512];
    assert(pop(slot,out)==36);
    assert(rd16(out+4)==NLMSG_ERROR && rd32(out+8)==seq);
    assert((int32_t)rd32(out+16)==error);
    assert(rd32(out+20)==16 || rd32(out+20)>=16);
    assert(rd32(out+28)==seq); /* embedded original header sequence */
}
static uint32_t generation(unsigned slot) {
    uint8_t req[512],out[512];
    unsigned n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETGEN,
                        0,NFPROTO_UNSPEC);
    const uint32_t seq=rd32(req+8);
    send_one(slot,req,n);
    assert(pop(slot,out)==28);
    /* Linux kernel uses NEWGEN (not GETGEN) as the response operation. */
    assert(rd16(out+4)==((NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWGEN));
    assert(rd32(out+8)==seq && rd16(out+20)==8 && rd16(out+22)==NFTA_GEN_ID);
    const uint32_t value=be32(out+24);
    assert(out[18]==(uint8_t)(value>>8) && out[19]==(uint8_t)value);
    empty(slot);
    return value;
}
static uint64_t assert_table(unsigned slot,uint8_t family,const char *name,
                             uint32_t seq,bool multi) {
    uint8_t out[512];
    unsigned n=pop(slot,out);
    assert(n>=40 && rd16(out+4)==((NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWTABLE));
    assert(rd32(out+8)==seq && out[16]==family);
    assert(rd16(out+6)==(multi?NLM_F_MULTI:0));
    unsigned attr=20;
    assert(rd16(out+attr+2)==NFTA_TABLE_NAME);
    assert(rd16(out+attr)==strlen(name)+5U);
    assert(!strcmp((const char *)(out+attr+4),name));
    attr+=align4(rd16(out+attr));
    assert(rd16(out+attr)==8 && rd16(out+attr+2)==NFTA_TABLE_USE);
    assert(be32(out+attr+4)==0); /* actually zero chains */
    attr+=8;
    assert(rd16(out+attr)==12 && rd16(out+attr+2)==NFTA_TABLE_HANDLE);
    uint64_t handle=be64(out+attr+4);
    assert(handle!=0); /* real stable table identity, never a fake zero */
    attr+=12;
    assert(rd16(out+attr)==8 && rd16(out+attr+2)==NFTA_TABLE_FLAGS);
    assert(be32(out+attr+4)==0); /* no dormant/owner/persistence */
    assert(attr+8==n);
    return handle;
}
static void expect_done(unsigned slot,uint32_t seq) {
    uint8_t out[512];
    assert(pop(slot,out)==20);
    assert(rd16(out+4)==NLMSG_DONE && rd16(out+6)==NLM_F_MULTI);
    assert(rd32(out+8)==seq && rd32(out+16)==0);
}
static void table_mut(unsigned slot,unsigned op,const char *name,
                       uint16_t flags,int expected) {
    uint8_t req[512];
    unsigned n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|(uint16_t)op,
                        NLM_F_ACK|flags,NFPROTO_INET);
    n=add_name(req,n,name);
    send_one(slot,req,n);
    ack_error(slot,rd32(req+8),expected);
    empty(slot);
}
static uint64_t lookup(unsigned slot,uint8_t family,const char *name,
                       bool dump,int expected) {
    uint8_t req[512];
    unsigned n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETTABLE,
                        dump?NLM_F_DUMP:0,family);
    if(name)n=add_name(req,n,name);
    uint32_t seq=rd32(req+8);
    send_one(slot,req,n);
    uint64_t handle=0;
    if(expected)ack_error(slot,seq,expected);
    else {
        assert(name);
        handle=assert_table(slot,family,name,seq,dump);
        if(dump)expect_done(slot,seq);
    }
    empty(slot);
    return handle;
}
static unsigned batch_begin(uint8_t *req,uint32_t expected) {
    unsigned n=make_msg(req,NFNL_MSG_BATCH_BEGIN,0,NFPROTO_UNSPEC);
    req[18]=NFNL_SUBSYS_NFTABLES; /* old libnftnl host order compatibility */
    if(expected)n=add_genid(req,n,expected);
    return n;
}
static unsigned append(uint8_t *datagram,unsigned off,
                       const uint8_t *msg,unsigned length) {
    assert(off+align4(length)<=NAS_NETLINK_FILTER_MAX_DATAGRAM);
    memcpy(datagram+off,msg,length);
    if(align4(length)>length)
        memset(datagram+off+length,0,align4(length)-length);
    return off+align4(length);
}
static unsigned batch_end(uint8_t *req) {
    unsigned n=make_msg(req,NFNL_MSG_BATCH_END,0,NFPROTO_UNSPEC);
    req[18]=NFNL_SUBSYS_NFTABLES;
    return n;
}
static unsigned batch_table(uint8_t *req,const char *name,unsigned op,
                            uint32_t *seq) {
    unsigned n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|(uint16_t)op,
                        NLM_F_ACK|NLM_F_CREATE|NLM_F_EXCL,NFPROTO_INET);
    n=add_name(req,n,name);
    *seq=rd32(req+8);
    return n;
}

int main(void) {
    nas_current_task=&task;
    task.credentials.cap_initialized=true;
    task.credentials.cap_effective=NAS_CAP_NET_RAW;
    task.credentials.cap_permitted=NAS_CAP_NET_RAW|(UINT64_C(1)<<12);
    nas_open_files[10].refcount=1;
    nas_open_files[11].refcount=1;
    assert(nas_netlink_filter_open(10,100)==0);
    assert(nas_netlink_filter_open(11,100)==0);
    assert(nas_netlink_filter_protocol(10)==12);
    assert(nas_netlink_filter_is_slot(10));
    assert(!nas_netlink_filter_is_slot(12));

    /* Both reads and mutation require the effective Linux CAP_NET_ADMIN. */
    uint8_t req[512],out[512];
    unsigned n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETGEN,
                        0,NFPROTO_UNSPEC);
    send_one(10,req,n);
    ack_error(10,rd32(req+8),-EPERM);
    task.credentials.cap_effective|=UINT64_C(1)<<12;
    uint32_t start=generation(10);
    assert(start>0);

    /* Actual lifecycle, visible to both sockets; no synthesized content. */
    table_mut(10,NFT_MSG_NEWTABLE,"m206",NLM_F_CREATE|NLM_F_EXCL,0);
    uint64_t first_handle=lookup(11,NFPROTO_INET,"m206",false,0);
    assert(lookup(11,NFPROTO_INET,"m206",true,0)==first_handle);
    assert(generation(11)==start+1);
    table_mut(10,NFT_MSG_NEWTABLE,"m206",NLM_F_CREATE|NLM_F_EXCL,-EEXIST);
    table_mut(10,NFT_MSG_DELTABLE,"missing",0,-ENOENT);
    table_mut(10,NFT_MSG_DELTABLE,"m206",0,0);
    lookup(11,NFPROTO_INET,"m206",false,-ENOENT);
    assert(generation(10)==start+2);

    /* Dump empty real registry (table + chain/rule/set/obj types). */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETTABLE,
               NLM_F_DUMP,NFPROTO_UNSPEC);
    uint32_t seq=rd32(req+8);
    send_one(10,req,n);expect_done(10,seq);empty(10);
    const unsigned empty_ops[]={NFT_MSG_GETCHAIN,NFT_MSG_GETRULE,
        NFT_MSG_GETSET,NFT_MSG_GETSETELEM,NFT_MSG_GETOBJ,
        NFT_MSG_GETFLOWTABLE};
    for(unsigned i=0;i<sizeof(empty_ops)/sizeof(empty_ops[0]);i++) {
        n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|(uint16_t)empty_ops[i],
                   NLM_F_DUMP,NFPROTO_UNSPEC);
        seq=rd32(req+8);send_one(10,req,n);expect_done(10,seq);empty(10);
    }

    /* Original libnftnl sends a TABLE NLA for the GETSET dump behind
     * `nft list table inet m206`.  A valid scoped empty query must return
     * DONE (and the requested ACK), never EOPNOTSUPP or imaginary objects. */
    table_mut(10,NFT_MSG_NEWTABLE,"m206",NLM_F_CREATE|NLM_F_EXCL,0);
    /* Exact M210 original Alpine `nft list table inet m206` first request:
     * seq=0, GETTABLE + REQUEST|ACK + NFTA_TABLE_NAME. Linux returns a
     * NEWTABLE (NAME,USE,HANDLE,FLAGS) immediately followed by ACK(0). */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETTABLE,
               NLM_F_ACK,NFPROTO_INET);
    wr32(req+8,0);
    n=add_name(req,n,"m206");
    assert(n==32);
    send_one(10,req,n);
    assert_table(10,NFPROTO_INET,"m206",0,false);
    ack_error(10,0,0);
    empty(10);
    /* Exact subsequent userspace dump order, with TABLE-only filter for
     * all except the unscoped empty GETCHAIN; each emits an empty DONE. */
    const unsigned original_list_ops[]={NFT_MSG_GETCHAIN,NFT_MSG_GETSET,
        NFT_MSG_GETOBJ,NFT_MSG_GETFLOWTABLE,NFT_MSG_GETRULE};
    for(unsigned i=0;i<sizeof(original_list_ops)/sizeof(original_list_ops[0]);i++) {
        n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|original_list_ops[i],
                   NLM_F_DUMP,i==0?NFPROTO_UNSPEC:NFPROTO_INET);
        wr32(req+8,0);
        if(i!=0)n=add_string_attr(req,n,1,"m206");
        send_one(10,req,n);
        expect_done(10,0);
        empty(10);
    }
    for(unsigned i=0;i<sizeof(empty_ops)/sizeof(empty_ops[0]);i++) {
        n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|(uint16_t)empty_ops[i],
                   NLM_F_DUMP|NLM_F_ACK,NFPROTO_INET);
        n=add_string_attr(req,n,1,"m206");
        if(empty_ops[i]==NFT_MSG_GETCHAIN)
            n=add_string_attr(req,n,NFTA_CHAIN_NAME,"absent-chain");
        else if(empty_ops[i]==NFT_MSG_GETRULE)
            n=add_string_attr(req,n,NFTA_RULE_CHAIN,"absent-chain");
        else if(empty_ops[i]==NFT_MSG_GETSETELEM)
            n=add_string_attr(req,n,NFTA_SET_ELEM_LIST_SET,"absent-set");
        else if(empty_ops[i]==NFT_MSG_GETOBJ)
            n=add_obj_type(req,n,NFT_OBJECT_COUNTER);
        else if(empty_ops[i]==NFT_MSG_GETFLOWTABLE)
            n=add_string_attr(req,n,NFTA_FLOWTABLE_NAME,"absent-flow");
        seq=rd32(req+8);
        send_one(10,req,n);
        expect_done(10,seq);
        ack_error(10,seq,0);
        empty(10);
    }
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSET,
               NLM_F_DUMP|NLM_F_ACK,NFPROTO_INET);
    n=add_string_attr(req,n,NFTA_SET_TABLE,"m206");
    n=add_string_attr(req,n,NFTA_SET_TABLE,"m206");
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EINVAL);empty(10); /* repeated selector rejected */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSET,
               NLM_F_DUMP|NLM_F_ACK,NFPROTO_INET);
    n=add_string_attr(req,n,NFTA_SET_NAME,"not-a-dump-filter");
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EOPNOTSUPP);empty(10); /* unknown dump selector */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETSET,
               NLM_F_DUMP|NLM_F_ACK,NFPROTO_INET);
    wr16(req+n,9);wr16(req+n+2,NFTA_SET_TABLE);
    memcpy(req+n+4,"m206x",5); /* missing trailing NUL */
    wr32(req,n+12);n+=12;
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EINVAL);empty(10);
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETOBJ,
               NLM_F_DUMP|NLM_F_ACK,NFPROTO_INET);
    n=add_string_attr(req,n,NFTA_OBJ_TABLE,"m206");
    n=add_obj_type(req,n,NFT_OBJECT_MAX+1U);
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EOPNOTSUPP);empty(10);
    table_mut(10,NFT_MSG_DELTABLE,"m206",0,0);

    /* An nft -c check omits BATCH_END, so Linux aborts on same skb while
     * ACKing valid syntax; validating must not create any table. */
    uint8_t datagram[NAS_NETLINK_FILTER_MAX_DATAGRAM];
    uint8_t part[512];
    unsigned off=0;
    n=batch_begin(part,generation(10));off=append(datagram,off,part,n);
    uint32_t seq1;
    n=batch_table(part,"checked",NFT_MSG_NEWTABLE,&seq1);
    off=append(datagram,off,part,n);
    send_one(10,datagram,off);ack_error(10,seq1,0);empty(10);
    lookup(11,NFPROTO_INET,"checked",false,-ENOENT);

    /* M210 Linux-strace oracle, from the actual signed Alpine nft ELF:
     * sendmsg([BATCH_BEGIN seq=0, flags=REQUEST, BE res_id=10],
     *         [NFT_NEWTABLE seq=1, flags=REQUEST only,
     *          family=NFPROTO_INET, NAME="m206", FLAGS=0]), no BATCH_END.
     * Linux exits 0 without a reply; its speculative creation is aborted.
     * Importantly the NEWTABLE does NOT carry NLM_F_CREATE or NLM_F_ACK. */
    uint32_t dryrun_generation=generation(10);
    off=0;
    n=batch_begin(part,0);
    wr32(part+8,0);
    part[18]=0;part[19]=NFNL_SUBSYS_NFTABLES;
    off=append(datagram,off,part,n);
    n=make_msg(part,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWTABLE,
               0,NFPROTO_INET);
    wr32(part+8,1);
    n=add_name(part,n,"m206");
    n=add_table_flags(part,n,0);
    assert(n==40); /* same wire lengths as real Linux strace */
    off=append(datagram,off,part,n);
    assert(off==60);
    send_one(10,datagram,off);
    empty(10); /* no reply when no ACK requested */
    lookup(11,NFPROTO_INET,"m206",false,-ENOENT);
    assert(generation(10)==dryrun_generation);

    /* Same genuine unflagged NEWTABLE with a normal END must commit;
     * this distinguishes actual table creation from a fake dry-run ACK. */
    off=0;
    n=batch_begin(part,0);
    wr32(part+8,0);
    part[18]=0;part[19]=NFNL_SUBSYS_NFTABLES;
    off=append(datagram,off,part,n);
    n=make_msg(part,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWTABLE,
               0,NFPROTO_INET);
    wr32(part+8,1);
    n=add_name(part,n,"m206");
    n=add_table_flags(part,n,0);
    off=append(datagram,off,part,n);
    n=batch_end(part);
    off=append(datagram,off,part,n);
    send_one(10,datagram,off);
    empty(10);
    uint64_t committed_handle=lookup(11,NFPROTO_INET,"m206",false,0);
    assert(committed_handle>first_handle);
    assert(generation(10)==dryrun_generation+1U);
    /* Exact M210 original Alpine delete 72-byte batch: an unflagged
     * NFT_DELTABLE plus BATCH_END, neither of which requests an ACK. */
    off=0;
    n=batch_begin(part,0);
    wr32(part+8,0);
    part[18]=0;part[19]=NFNL_SUBSYS_NFTABLES;
    off=append(datagram,off,part,n);
    n=make_msg(part,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_DELTABLE,
               0,NFPROTO_INET);
    wr32(part+8,1);
    n=add_name(part,n,"m206");
    assert(n==32);
    off=append(datagram,off,part,n);
    n=batch_end(part);
    wr32(part+8,2);
    off=append(datagram,off,part,n);
    assert(off==72);
    send_one(10,datagram,off);
    empty(10);
    lookup(11,NFPROTO_INET,"m206",false,-ENOENT);
    assert(generation(10)==dryrun_generation+2U);

    /* Real batch commits only when BEGIN+changes+END share one skb. */
    off=0;
    n=batch_begin(part,generation(10));off=append(datagram,off,part,n);
    n=batch_table(part,"batchtable",NFT_MSG_NEWTABLE,&seq1);
    off=append(datagram,off,part,n);
    n=batch_end(part);off=append(datagram,off,part,n);
    send_one(10,datagram,off);ack_error(10,seq1,0);empty(10);
    lookup(11,NFPROTO_INET,"batchtable",false,0);

    /* The second transaction fails EEXIST: first staged create rolls back. */
    uint32_t unchanged=generation(10),seq2,seq3;
    off=0;
    n=batch_begin(part,unchanged);off=append(datagram,off,part,n);
    n=batch_table(part,"rollback",NFT_MSG_NEWTABLE,&seq2);
    off=append(datagram,off,part,n);
    n=batch_table(part,"batchtable",NFT_MSG_NEWTABLE,&seq3);
    off=append(datagram,off,part,n);
    n=batch_end(part);off=append(datagram,off,part,n);
    send_one(10,datagram,off);
    /* Linux ACK reports per-op validation; successful staged work may be
     * ACKed 0 although the complete failing transaction is rolled back. */
    ack_error(10,seq2,0);
    ack_error(10,seq3,-EEXIST);
    empty(10);
    lookup(11,NFPROTO_INET,"rollback",false,-ENOENT);
    assert(generation(11)==unchanged);

    /* A real kernel validates every command in a batch. An unsupported
     * chain operation must abort a valid staged table and preserve the
     * original request ordering of its success ACK and the chain error. */
    off=0;
    n=batch_begin(part,unchanged);off=append(datagram,off,part,n);
    n=batch_table(part,"nohooks",NFT_MSG_NEWTABLE,&seq2);
    off=append(datagram,off,part,n);
    n=make_msg(part,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWCHAIN,
               NLM_F_ACK|NLM_F_CREATE,NFPROTO_INET);
    seq3=rd32(part+8);off=append(datagram,off,part,n);
    n=batch_end(part);off=append(datagram,off,part,n);
    send_one(10,datagram,off);
    ack_error(10,seq2,0);
    ack_error(10,seq3,-EOPNOTSUPP);
    empty(10);
    lookup(11,NFPROTO_INET,"nohooks",false,-ENOENT);
    assert(generation(11)==unchanged);

    /* Generation ID is checked by the kernel before accepting a batch. */
    n=batch_begin(req,unchanged-1);
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EAGAIN);empty(10);

    /* Never ACK a table operation if the claimed type is unsupported. */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWCHAIN,
               NLM_F_ACK|NLM_F_CREATE,NFPROTO_INET);
    seq=rd32(req+8);send_one(10,req,n);
    ack_error(10,seq,-EOPNOTSUPP);empty(10);
    /* NLA boundary errors produce a corresponding nlmsgerr, no mutation. */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_NEWTABLE,
               NLM_F_ACK|NLM_F_CREATE,NFPROTO_INET);
    wr16(req+n,3);wr16(req+n+2,NFTA_TABLE_NAME);
    wr32(req,n+4);seq=rd32(req+8);send_one(10,req,n+4);
    ack_error(10,seq,-EINVAL);empty(10);

    /* Actual socket identity, recvmsg peek/trunc and lifetime cleanup. */
    uint8_t addr[12]={0};
    wr16(addr,16);wr32(addr+4,8888);
    assert(nas_netlink_filter_bind(10,addr,sizeof(addr))==-EINVAL);
    assert(nas_netlink_filter_getsockname(10,addr)==0);
    assert(rd32(addr+4)==100); /* auto-bound by the first send */
    n=make_msg(req,(NFNL_SUBSYS_NFTABLES<<8)|NFT_MSG_GETGEN,
               0,NFPROTO_UNSPEC);
    send_one(10,req,n);
    uint32_t source_len=0,msg_flags=0;
    assert(nas_netlink_filter_recvmsg(10,out,5,MSG_PEEK|MSG_TRUNC,
                                      addr,&source_len,&msg_flags)==28);
    assert(source_len==12 && msg_flags==MSG_TRUNC);
    assert(rd32(addr+4)==0);
    assert(pop(10,out)==28);empty(10);
    nas_netlink_filter_ops()->release(10);
    assert(!nas_netlink_filter_is_slot(10));
    assert(nas_netlink_filter_is_slot(11));
    table_mut(11,NFT_MSG_DELTABLE,"batchtable",0,0);
    nas_netlink_filter_ops()->release(11);
    puts("NETLINK_NETFILTER real table lifecycle, generation, CAP, batch, rollback and wire: PASS");
    return 0;
}
