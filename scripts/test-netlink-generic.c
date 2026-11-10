/* Host wire and OpenFile test of the production NETLINK_GENERIC controller.
 * Linux netlink/genetlink UAPI is the oracle; no special test-only provider. */
#include "../kernel/fs/netlink_generic.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PORT_POLLIN 1U
#define PORT_POLLOUT 4U

_Static_assert(NAS_NETLINK_GENERIC_PROTOCOL==NETLINK_GENERIC,
               "Linux NETLINK_GENERIC protocol");
_Static_assert(GENL_ID_CTRL==16U,"Linux controller family ID");
_Static_assert(GENL_HDRLEN==4U,"Linux generic header");

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task;
static struct nas_task task;
static struct nas_files files;
static uint32_t sequence;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void wr16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static unsigned align4(unsigned n) {return (n+3U)&~3U;}
static unsigned request(uint8_t *p,uint16_t type,uint16_t flags,uint8_t cmd) {
    memset(p,0,256);
    wr32(p,20);wr16(p+4,type);wr16(p+6,(uint16_t)(flags|NLM_F_REQUEST));
    wr32(p+8,++sequence);p[16]=cmd;
    return 20;
}
static unsigned add_string(uint8_t *p,unsigned off,uint16_t type,
                           const char *value) {
    unsigned len=(unsigned)strlen(value)+1U;
    wr16(p+off,(uint16_t)(len+4U));wr16(p+off+2,type);
    memcpy(p+off+4,value,len);
    unsigned next=off+align4(len+4U);
    wr32(p,next);
    return next;
}
static unsigned add_id(uint8_t *p,unsigned off,uint16_t id) {
    wr16(p+off,6);wr16(p+off+2,CTRL_ATTR_FAMILY_ID);
    wr16(p+off+4,id);
    wr32(p,off+8);
    return off+8;
}
static void send_request(unsigned slot,const uint8_t *bytes,unsigned size) {
    assert(nas_netlink_generic_sendto(slot,bytes,size,NULL,0)==(int64_t)size);
}
static unsigned recv_one(unsigned slot,uint8_t *out) {
    int64_t rc=nas_netlink_generic_recvmsg(slot,out,256,0,NULL,NULL,NULL);
    assert(rc>=20 && rc<=128);
    assert(rd32(out)==(uint32_t)rc);
    return (unsigned)rc;
}
static void empty(unsigned slot) {
    uint8_t out[256];
    assert(nas_netlink_generic_recvmsg(slot,out,sizeof(out),MSG_DONTWAIT,
                                       NULL,NULL,NULL)==-EAGAIN);
}
static void error_response(unsigned slot,uint32_t seq,int error) {
    uint8_t out[256];
    assert(recv_one(slot,out)==36);
    assert(rd16(out+4)==NLMSG_ERROR && rd32(out+8)==seq);
    assert((int32_t)rd32(out+16)==error);
    assert(rd32(out+28)==seq);
}
static void family_response(unsigned slot,uint32_t seq,bool dump) {
    uint8_t out[256];
    unsigned n=recv_one(slot,out);
    assert(n>=64 && n<=128 && rd16(out+4)==GENL_ID_CTRL);
    assert(rd32(out+8)==seq && out[16]==CTRL_CMD_NEWFAMILY);
    assert(out[17]==2 && rd16(out+18)==0);
    assert(rd16(out+6)==(dump?NLM_F_MULTI:0));
    bool found_name=false,found_id=false,found_ver=false,found_hdr=false,
         found_maxattr=false,found_ops=false;
    for(unsigned off=20;off<n;) {
        unsigned len=rd16(out+off),type=rd16(out+off+2)&NLA_TYPE_MASK;
        assert(len>=4 && len<=n-off);
        switch(type) {
        case CTRL_ATTR_FAMILY_NAME:
            assert(len==11 && !memcmp(out+off+4,"nlctrl",7));
            found_name=true;break;
        case CTRL_ATTR_FAMILY_ID:
            assert(len==6 && rd16(out+off+4)==GENL_ID_CTRL);
            found_id=true;break;
        case CTRL_ATTR_VERSION:
            assert(len==8 && rd32(out+off+4)==2U);
            found_ver=true;break;
        case CTRL_ATTR_HDRSIZE:
            assert(len==8 && rd32(out+off+4)==0);
            found_hdr=true;break;
        case CTRL_ATTR_MAXATTR:
            assert(len==8 && rd32(out+off+4)>=CTRL_ATTR_FAMILY_NAME);
            found_maxattr=true;break;
        case CTRL_ATTR_OPS: {
            assert(len>=12 && !(rd16(out+off+2)&NLA_F_NESTED));
            unsigned item=off+4,item_len=rd16(out+item);
            assert((rd16(out+item+2)&NLA_TYPE_MASK)==1 &&
                   item_len<=len-4);
            unsigned id=0,flags=0;
            for(unsigned inner=item+4;inner<item+item_len;) {
                unsigned ilen=rd16(out+inner),itype=rd16(out+inner+2);
                assert(ilen==8);
                if(itype==CTRL_ATTR_OP_ID)id=rd32(out+inner+4);
                else if(itype==CTRL_ATTR_OP_FLAGS)flags=rd32(out+inner+4);
                else assert(!"fabricated operation attribute");
                inner+=align4(ilen);
            }
            assert(id==CTRL_CMD_GETFAMILY);
            assert(flags==(GENL_CMD_CAP_DO|GENL_CMD_CAP_DUMP));
            found_ops=true;break;
        }
        default:assert(!"fabricated or unknown controller attribute");
        }
        off+=align4(len);
    }
    assert(found_name && found_id && found_ver && found_hdr &&
           found_maxattr && found_ops);
}
static void done_response(unsigned slot,uint32_t seq) {
    uint8_t out[256];
    assert(recv_one(slot,out)==20);
    assert(rd16(out+4)==NLMSG_DONE && rd16(out+6)==NLM_F_MULTI);
    assert(rd32(out+8)==seq && rd32(out+16)==0);
}
static void receive_semantics(unsigned slot) {
    uint8_t req[256],buf[256],source[12];
    unsigned n=request(req,GENL_ID_CTRL,NLM_F_DUMP,CTRL_CMD_GETFAMILY);
    uint32_t seq=rd32(req+8);
    send_request(slot,req,n);
    uint32_t addrlen=0,flags=0;
    assert(nas_netlink_generic_recvmsg(slot,buf,8,MSG_PEEK|MSG_TRUNC,
                                       source,&addrlen,&flags)>8);
    assert(addrlen==12 && (flags&MSG_TRUNC) && rd16(source)==AF_NETLINK);
    assert(rd32(source+4)==0 && rd32(source+8)==0);
    assert(rd16(buf+4)==GENL_ID_CTRL);
    family_response(slot,seq,true); /* PEEK did not consume the first reply. */
    done_response(slot,seq);
    empty(slot);
}
static void close_slot(unsigned slot) {
    nas_netlink_generic_ops()->release(slot);
    assert(!nas_netlink_generic_is_slot(slot));
    nas_open_files[slot].refcount=0;
}

int main(void) {
    nas_current_task=&task;
    task.fds=&files.table;
    nas_open_files[20].refcount=1;
    nas_open_files[21].refcount=1;
    assert(nas_netlink_generic_open(20,100)==0);
    assert(nas_netlink_generic_open(21,100)==0);
    assert(nas_netlink_generic_protocol(20)==NETLINK_GENERIC);
    assert(nas_netlink_generic_is_slot(20));
    assert(!nas_netlink_generic_is_slot(22));
    files.table.entries[7]=(struct nas_fd_entry){.kind=FD_SOCKET,.slot=20};
    const struct nas_file_ops *ops=nas_netlink_generic_ops();
    assert(ops && ops->release && ops->poll && ops->poll_description &&
           ops->read && ops->read_plan);
    assert((ops->poll(7,PORT_POLLOUT)&PORT_POLLOUT)!=0);
    assert((ops->poll_description(20,PORT_POLLIN)&PORT_POLLIN)==0);
    assert((int64_t)ops->read_plan(20,128)==-EAGAIN);

    uint8_t sockaddr[12]={16,0,0,0,100,0,0,0,0,0,0,0};
    uint8_t name[12];
    assert(nas_netlink_generic_getsockname(20,name)==0 && rd32(name+4)==0);
    sockaddr[8]=1;
    assert(nas_netlink_generic_bind(20,sockaddr,12)==-EOPNOTSUPP);
    sockaddr[8]=0;
    assert(nas_netlink_generic_bind(20,sockaddr,11)==-EINVAL);
    assert(nas_netlink_generic_bind(20,sockaddr,12)==0);
    assert(nas_netlink_generic_getsockname(20,name)==0 && rd32(name+4)==100);
    assert(nas_netlink_generic_bind(20,sockaddr,12)==-EINVAL);
    assert(nas_netlink_generic_bind(21,sockaddr,12)==-EADDRINUSE);
    memset(sockaddr+4,0,8);
    assert(nas_netlink_generic_bind(21,sockaddr,12)==0);
    assert(nas_netlink_generic_getsockname(21,name)==0 && rd32(name+4)!=100);

    assert(nas_netlink_generic_set_sndbuf(20,100)==0);
    uint32_t bytes=0;
    assert(nas_netlink_generic_get_sndbuf(20,&bytes)==0 && bytes==2048);
    assert(nas_netlink_generic_set_rcvbuf(20,1)==0);
    assert(nas_netlink_generic_get_rcvbuf(20,&bytes)==0 && bytes==128);
    assert(nas_netlink_generic_set_rcvbuf(20,4096)==0);
    assert(nas_netlink_generic_get_rcvbuf(20,&bytes)==0 && bytes==1024);

    uint8_t req[256];
    unsigned n=request(req,GENL_ID_CTRL,NLM_F_ACK,CTRL_CMD_GETFAMILY);
    n=add_string(req,n,CTRL_ATTR_FAMILY_NAME,"nlctrl");
    uint32_t seq=rd32(req+8);
    send_request(20,req,n);
    assert((ops->poll_description(20,PORT_POLLIN)&PORT_POLLIN)!=0);
    assert(ops->read_plan(20,128)>0);
    family_response(20,seq,false);
    error_response(20,seq,0);
    empty(20);

    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    n=add_id(req,n,GENL_ID_CTRL);seq=rd32(req+8);
    send_request(20,req,n);
    family_response(20,seq,false);
    empty(20);

    receive_semantics(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    n=add_string(req,n,CTRL_ATTR_FAMILY_NAME,"devlink");
    seq=rd32(req+8);send_request(20,req,n);
    error_response(20,seq,-ENOENT);empty(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    n=add_id(req,n,1234);seq=rd32(req+8);send_request(20,req,n);
    error_response(20,seq,-ENOENT);empty(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    seq=rd32(req+8);send_request(20,req,n);
    error_response(20,seq,-EINVAL);empty(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_NEWFAMILY);
    seq=rd32(req+8);send_request(20,req,n);
    error_response(20,seq,-EOPNOTSUPP);empty(20);
    n=request(req,GENL_ID_CTRL+4U,0,CTRL_CMD_GETFAMILY);
    seq=rd32(req+8);send_request(20,req,n);
    error_response(20,seq,-EOPNOTSUPP);empty(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    wr32(req,22);wr16(req+20,2);
    seq=rd32(req+8);send_request(20,req,24);
    error_response(20,seq,-EINVAL);empty(20);
    n=request(req,GENL_ID_CTRL,NLM_F_DUMP,CTRL_CMD_GETFAMILY);
    assert(n==20);
    wr32(req,48); /* Claim a longer Netlink payload than the datagram. */
    assert(nas_netlink_generic_sendto(20,req,n,NULL,0)==-EINVAL);
    empty(20);
    n=request(req,GENL_ID_CTRL,0,CTRL_CMD_GETFAMILY);
    n=add_id(req,n,GENL_ID_CTRL);
    sockaddr[4]=1;
    assert(nas_netlink_generic_sendto(20,req,n,sockaddr,12)==-EOPNOTSUPP);
    empty(20);

    close_slot(20);
    assert(nas_netlink_generic_open(20,100)==-EBADF);
    nas_open_files[20].refcount=1;
    assert(nas_netlink_generic_open(20,100)==0);
    assert(nas_netlink_generic_getsockname(20,name)==0 && rd32(name+4)==0);
    close_slot(20);
    close_slot(21);
    assert(nas_netlink_generic_protocol(20)==-EBADF);
    puts("[NETLINK GENERIC PASS] genuine nlctrl get name/id/dump, ops and errors");
    puts("[NETLINK GENERIC PASS] bind/port, SO buffers, PEEK/TRUNC, FD poll/release");
    return 0;
}
