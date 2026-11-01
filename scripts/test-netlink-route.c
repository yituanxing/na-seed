/* Host regression for the real kernel-buffer NETLINK_ROUTE provider.
 * This deliberately feeds a snapshot with the same source contract as the
 * native lwIP DHCP callback, and checks Linux multipart wire bytes and
 * actual FileOps lifetime. No guest userspace pointer or QEMU is involved. */
/* Compile with -DNAS_ENABLE_LWIP=1; default-kernel netlink_route.c is empty. */
#include "../kernel/fs/netlink_route.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../ports/lwip/network_boot.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task;
unsigned nas_lwip_guest_ipv4_ifconf(struct nas_lwip_guest_ipv4_if out[2]) {
    if(!out)return 0;
    out[0]=(struct nas_lwip_guest_ipv4_if){
        .name="lo",.ipv4_host_order=UINT32_C(0x7f000001),.prefix_len=8
    };
    out[1]=(struct nas_lwip_guest_ipv4_if){
        .name="eth0",.ipv4_host_order=UINT32_C(0x0a00020f),.prefix_len=24
    };
    return 2;
}
int nas_lwip_guest_ifindex(const char *name) {
    return name && !strcmp(name,"eth0") ? 2:-ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,char out[16]) {
    if(index!=2)return -ENODEV;
    memcpy(out,"eth0",5);return 0;
}

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
static void nl_sockaddr(uint8_t out[12],uint32_t pid,uint32_t groups) {
    memset(out,0,12);
    wr16(out,16);wr32(out+4,pid);wr32(out+8,groups);
}
static void getaddr(uint8_t out[20],uint8_t family,uint32_t seq) {
    memset(out,0,20);
    wr32(out,20);wr16(out+4,22);wr16(out+6,0x305);
    wr32(out+8,seq);out[16]=family;
}
static void expect_newaddr(const uint8_t *reply,uint32_t port,
                           uint32_t seq,unsigned index,uint8_t prefix,
                           unsigned scope,const uint8_t ip[4]) {
    assert(rd32(reply)==40 && rd16(reply+4)==20);
    assert(rd16(reply+6)==2 && rd32(reply+8)==seq && rd32(reply+12)==port);
    assert(reply[16]==2 && reply[17]==prefix && reply[19]==scope);
    assert(rd32(reply+20)==index);
    assert(rd16(reply+24)==8 && rd16(reply+26)==1);
    assert(rd16(reply+32)==8 && rd16(reply+34)==2);
    assert(!memcmp(reply+28,ip,4) && !memcmp(reply+36,ip,4));
}
int main(void) {
    const struct nas_file_ops *ops=nas_netlink_route_ops();
    assert(ops && ops->poll_description && ops->release);
    nas_open_files[11].refcount=1;
    nas_open_files[11].ops=ops;
    assert(nas_netlink_route_open(11,4142)==0);
    assert(nas_netlink_route_is_slot(11));
    uint8_t addr[12],name[12],request[20],reply[64],source[12];
    nl_sockaddr(addr,0,0x550); /* dnsmasq v2.91 multicast groups */
    assert(nas_netlink_route_bind(11,addr,12)==-EPERM);
    nl_sockaddr(addr,0,0);
    assert(nas_netlink_route_bind(11,addr,12)==0);
    assert(nas_netlink_route_bind(11,addr,12)==-EINVAL);
    assert(nas_netlink_route_getsockname(11,name)==0);
    assert(rd16(name)==16 && rd32(name+4)==4142 && rd32(name+8)==0);

    /* Real Linux auto-bind behavior: the TGID is used when available,
     * otherwise a distinct high kernel port identifier is assigned. */
    nas_open_files[12].refcount=1;nas_open_files[12].ops=ops;
    assert(nas_netlink_route_open(12,4142)==0);
    assert(nas_netlink_route_bind(12,addr,12)==0);
    assert(nas_netlink_route_getsockname(12,name)==0);
    assert(rd32(name+4)!=4142 && rd32(name+4)!=0);

    getaddr(request,10,101); /* no configured IPv6: honest empty dump */
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(ops->poll_description(11,1)&1);
    assert(ops->poll_description(11,4)&4);
    uint32_t source_len=0,msg_flags=0;
    memset(reply,0,sizeof(reply));
    assert(nas_netlink_route_recvmsg(11,reply,8,2|32,source,
                                    &source_len,&msg_flags)==16);
    assert(msg_flags==32 && source_len==12 && rd16(source)==16 &&
           rd32(source+4)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==16);
    assert(msg_flags==0 && rd32(reply)==16 && rd16(reply+4)==3);
    assert(rd32(reply+8)==101 && rd32(reply+12)==4142);
    assert(!(ops->poll_description(11,1)&1));

    getaddr(request,2,102);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(ops->poll_description(11,1)&1);
    assert(nas_netlink_route_recvmsg(11,reply,10,2|32,source,
                                    &source_len,&msg_flags)==40);
    assert(msg_flags==32);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==40);
    const uint8_t lo[4]={127,0,0,1};
    expect_newaddr(reply,4142,102,1,8,254,lo);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==40);
    const uint8_t ethernet[4]={10,0,2,15};
    expect_newaddr(reply,4142,102,2,24,0,ethernet);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==16);
    assert(rd16(reply+4)==3 && rd32(reply+8)==102);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==-EAGAIN);

    /* No partial multipart dumps if another pending request cannot fit. */
    getaddr(request,2,103);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EAGAIN);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==40);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==40);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==16);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    getaddr(request,2,104);
    wr32(request,21);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EINVAL);
    wr32(request,20);wr16(request+4,18); /* GETLINK unimplemented honestly */
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EOPNOTSUPP);
    assert(nas_netlink_route_sendto(11,request,4097,addr,12)==-EMSGSIZE);

    nas_open_files[11].refcount=0;
    ops->release(11);
    assert(!nas_netlink_route_is_slot(11));
    nas_open_files[11].refcount=1;
    assert(nas_netlink_route_open(11,9000)==0);
    assert(nas_netlink_route_bind(11,addr,12)==0);
    assert(nas_netlink_route_getsockname(11,name)==0);
    assert(rd32(name+4)==9000);
    puts("[NETLINK_ROUTE HOST PASS] real lo+DHCP IPv4 RTM_GETADDR multipart, "
         "SOCK_RAW port bind, MSG_PEEK/TRUNC, poll/release");
    return 0;
}
