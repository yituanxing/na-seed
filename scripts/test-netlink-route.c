/* Host regression for the real kernel-buffer NETLINK_ROUTE provider.
 * This deliberately feeds a snapshot with the same source contract as the
 * native lwIP DHCP callback, and checks Linux multipart wire bytes and
 * actual FileOps lifetime. No guest userspace pointer or QEMU is involved. */
/* The default kernel includes the real NETLINK_ROUTE File provider. */
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
static bool full_snapshot,second_addressed=true;
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
    if(!out)return 0;
    out[0]=(struct nas_lwip_guest_ipv4_if){
        .name="lo",.ifindex=1,
        .ipv4_host_order=UINT32_C(0x7f000001),.prefix_len=8
    };
    out[1]=(struct nas_lwip_guest_ipv4_if){
        .name="eth0",.ifindex=2,
        .ipv4_host_order=UINT32_C(0x0a00020f),.prefix_len=24
    };
    out[2]=(struct nas_lwip_guest_ipv4_if){
        .name="eth1",.ifindex=3,
        .ipv4_host_order=UINT32_C(0x0a000314),.prefix_len=24
    };
    if(full_snapshot){
        for(unsigned i=1;i<NAS_LWIP_GUEST_IF_LIMIT;i++){
            struct nas_lwip_guest_ipv4_if *nic=&out[i];
            memset(nic,0,sizeof(*nic));
            nic->name[0]='e';nic->name[1]='t';nic->name[2]='h';
            nic->name[3]=(char)('0'+i-1U);
            /* Non-sequential actual IDs catch accidental name-to-index
             * re-lookups after the one authoritative address snapshot. */
            nic->ifindex=100U+i;
            nic->ipv4_host_order=UINT32_C(0x0a000200)+i;
            nic->prefix_len=24;
        }
        return NAS_LWIP_GUEST_IF_LIMIT;
    }
    return second_addressed?3:2;
}
unsigned nas_lwip_guest_if_snapshot(
    struct nas_lwip_guest_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
    if(!out)return 0;
    out[0]=(struct nas_lwip_guest_if){.name="lo",.ifindex=1,
        .ipv4_host_order=UINT32_C(0x7f000001),.prefix_len=8,
        .mtu=65536,.up=true,.link_up=true,.loopback=true};
    for(unsigned i=1;i<(full_snapshot ? NAS_LWIP_GUEST_IF_LIMIT:3U);i++) {
        struct nas_lwip_guest_if *nic=&out[i];
        nic->name[0]='e';nic->name[1]='t';nic->name[2]='h';
        nic->name[3]=(char)('0'+i-1U);
        nic->ifindex=full_snapshot?100U+i:i+1U;
        nic->mtu=1500;
        nic->up=true;
        nic->link_up=full_snapshot || i==1 || second_addressed;
    }
    return full_snapshot ? NAS_LWIP_GUEST_IF_LIMIT:3U;
}
int nas_lwip_guest_ifindex(const char *name) {
    (void)name;
    /* NETLINK_ROUTE must never re-read an interface by name after a
     * snapshot: a DHCP update could change the second observation. */
    assert(!"NETLINK_ROUTE retried netif identity outside snapshot");
    return -ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,char out[16]) {
    if(index==2){memcpy(out,"eth0",5);return 0;}
    if(index==3){memcpy(out,"eth1",5);return 0;}
    return -ENODEV;
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
static void getlink(uint8_t out[32],uint8_t family,uint32_t seq) {
    memset(out,0,32);
    wr32(out,32);wr16(out+4,18);wr16(out+6,0x305);
    wr32(out+8,seq);out[16]=family;
}
static void expect_newlink(const uint8_t *reply,uint32_t port,
                           uint32_t seq,uint32_t index,uint16_t type,
                           uint32_t flags,const char *name,uint32_t mtu) {
    const unsigned n=(unsigned)strlen(name)+1U;
    const unsigned attr=4U+n;
    const unsigned mtu_off=32U+((attr+3U)&~3U);
    assert(rd32(reply)==mtu_off+8U && rd16(reply+4)==16);
    assert(rd16(reply+6)==2 && rd32(reply+8)==seq && rd32(reply+12)==port);
    assert(reply[16]==0 && rd16(reply+18)==type && rd32(reply+20)==index);
    assert(rd32(reply+24)==flags && rd32(reply+28)==0);
    assert(rd16(reply+32)==attr && rd16(reply+34)==3);
    assert(!memcmp(reply+36,name,n));
    assert(rd16(reply+mtu_off)==8 && rd16(reply+mtu_off+2)==4 &&
           rd32(reply+mtu_off+4)==mtu);
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
    uint32_t sndbuf=0;
    assert(nas_netlink_route_get_sndbuf(11,&sndbuf)==0 &&
           sndbuf==NAS_NETLINK_ROUTE_MAX_DATAGRAM);
    assert(nas_netlink_route_get_sndbuf(11,NULL)==-EINVAL);
    assert(nas_netlink_route_set_sndbuf(11,1)==0);
    assert(nas_netlink_route_get_sndbuf(11,&sndbuf)==0 && sndbuf==2048);
    assert(nas_netlink_route_set_sndbuf(11,8192)==0);
    assert(nas_netlink_route_get_sndbuf(11,&sndbuf)==0 && sndbuf==16384);
    assert(nas_netlink_route_set_sndbuf(11,UINT32_MAX)==0);
    assert(nas_netlink_route_get_sndbuf(11,&sndbuf)==0 && sndbuf==262144);
    assert(nas_netlink_route_set_sndbuf(11,1)==0);
    uint32_t rcvbuf=0;
    const uint32_t real_rx_bytes=(NAS_LWIP_GUEST_IF_LIMIT+1U)*64U;
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 &&
           rcvbuf==real_rx_bytes);
    assert(nas_netlink_route_get_rcvbuf(11,NULL)==-EINVAL);
    assert(nas_netlink_route_set_rcvbuf(11,1)==0);
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 && rcvbuf==64);
    uint8_t addr[12],name[12],request[20],reply[64],source[12];
    nl_sockaddr(addr,0,0x550); /* dnsmasq v2.91 multicast groups */
    assert(nas_netlink_route_bind(11,addr,12)==-EPERM);
    nl_sockaddr(addr,0,0);
    assert(nas_netlink_route_bind(11,addr,12)==0);
    assert(nas_netlink_route_bind(11,addr,12)==-EINVAL);
    getaddr(request,2,99);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),addr,12)==
           -EAGAIN); /* four records cannot fit one 64B RCVBUF */
    getaddr(request,10,99);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),addr,12)==20);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(nas_netlink_route_set_rcvbuf(11,UINT32_MAX)==0);
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 &&
           rcvbuf==real_rx_bytes);
    /* Original iproute2 calls send() without a sockaddr_nl for route
     * dumps. Linux implicitly selects the kernel port (zero); reject only
     * malformed explicit destinations and unsupported peer/multicast. */
    getaddr(request,10,99);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),NULL,0)==20);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd32(reply)==20 && rd16(reply+4)==3 &&
           rd32(reply+8)==99 && rd32(reply+16)==0);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),NULL,12)==
           -EINVAL);
    nl_sockaddr(addr,123,0);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),addr,12)==
           -EOPNOTSUPP);
    nl_sockaddr(addr,0,1);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),addr,12)==
           -EOPNOTSUPP);
    nl_sockaddr(addr,0,0);
    assert(nas_netlink_route_set_rcvbuf(11,64)==0);
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 && rcvbuf==128);
    assert(nas_netlink_route_set_rcvbuf(11,UINT32_MAX)==0);
    /* A smaller SO_SNDBUF quota must actually reject an outbound datagram
     * that otherwise fits the provider's independent 4096-byte hard limit. */
    uint8_t padded[3000]={0};
    getaddr(padded,10,100);
    assert(nas_netlink_route_sendto(11,padded,sizeof(padded),addr,12)==
           -EMSGSIZE);
    assert(nas_netlink_route_set_sndbuf(11,8192)==0);
    assert(nas_netlink_route_sendto(11,padded,sizeof(padded),addr,12)==3000);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
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
                                    &source_len,&msg_flags)==20);
    assert(msg_flags==32 && source_len==12 && rd16(source)==16 &&
           rd32(source+4)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==20);
    assert(msg_flags==0 && rd32(reply)==20 && rd16(reply+4)==3 &&
           rd32(reply+16)==0);
    assert(rd32(reply+8)==101 && rd32(reply+12)==4142);
    assert(!(ops->poll_description(11,1)&1));

    /* RTM_GETLINK returns live native link metadata, including an exact
     * IFLA_IFNAME and IFLA_MTU, never invented MACs or TX statistics. */
    uint8_t link_request[32];
    getlink(link_request,0,201);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==48);
    expect_newlink(reply,4142,201,1,772,0x10049,"lo",65536);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==52);
    expect_newlink(reply,4142,201,2,1,0x10041,"eth0",1500);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==52);
    expect_newlink(reply,4142,201,3,1,0x10041,"eth1",1500);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==201 && rd32(reply+16)==0);

    /* Stock iproute2 can request all address families. AF_UNSPEC reports
     * the real assigned IPv4 addresses; unsupported IPv6 is not invented. */
    getaddr(request,0,203);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    for(unsigned i=0;i<3;i++) {
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==40);
        assert(rd16(reply+4)==20 && rd32(reply+8)==203 &&
               rd32(reply+20)==i+1U && reply[16]==2);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);

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
                                    &source_len,&msg_flags)==40);
    const uint8_t second[4]={10,0,3,20};
    expect_newaddr(reply,4142,102,3,24,0,second);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==102 && rd32(reply+16)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==-EAGAIN);

    /* No partial multipart dumps if another pending request cannot fit. */
    getaddr(request,2,103);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EAGAIN);
    for(unsigned i=0;i<2;i++){
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==40);
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==40);
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==40);
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==20);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* Losing a DHCP lease removes only that address; the interface itself
     * still exists, and an exact one-shot address dump must not relabel it. */
    second_addressed=false;
    getaddr(request,2,106);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==40);
    expect_newaddr(reply,4142,106,1,8,254,lo);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==40);
    expect_newaddr(reply,4142,106,2,24,0,ethernet);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==106 && rd32(reply+16)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* A lost DHCP address does not delete its physical link identity. */
    getlink(link_request,0,202);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    for(unsigned i=0;i<3;i++) {
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==(i?52:48));
        expect_newlink(reply,4142,202,i+1U,i?1U:772U,
                       i==2?1U:i?0x10041U:0x10049U,
                       i==0?"lo":i==1?"eth0":"eth1",i?1500U:65536U);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    second_addressed=true;
    /* Full registry capacity: lo + eight independently addressed NICs
     * must fit one all-or-nothing multipart dump, including NLMSG_DONE. */
    full_snapshot=true;
    getaddr(request,2,105);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==20);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EAGAIN);
    for(unsigned i=0;i<NAS_LWIP_GUEST_IF_LIMIT;i++){
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==40);
        const uint8_t ip[4]={i?10U:127U,i?0U:0U,
                             i?2U:0U,i?(uint8_t)i:1U};
        expect_newaddr(reply,4142,105,i?100U+i:1U,i?24U:8U,
                       i?0U:254U,ip);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==105 && rd32(reply+16)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* Full eight-NIC link dump fits the fixed 10-record ring atomically. */
    getlink(link_request,0,205);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==-EAGAIN);
    for(unsigned i=0;i<NAS_LWIP_GUEST_IF_LIMIT;i++) {
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==(i?52:48));
        char name[5]="eth0";
        if(i)name[3]=(char)('0'+i-1U);
        expect_newlink(reply,4142,205,i?100U+i:1U,i?1U:772U,
                       i?0x10041U:0x10049U,i?name:"lo",i?1500U:65536U);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    full_snapshot=false;
    getaddr(request,2,104);
    wr32(request,21);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EINVAL);
    wr32(request,20);wr16(request+4,26); /* RTM_GETROUTE unsupported */
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EOPNOTSUPP);
    getlink(link_request,0,206);
    wr32(link_request,31); /* a partial ifinfomsg cannot be decoded */
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==-EINVAL);
    assert(nas_netlink_route_sendto(11,request,4097,addr,12)==-EMSGSIZE);

    nas_open_files[11].refcount=0;
    ops->release(11);
    assert(!nas_netlink_route_is_slot(11));
    nas_open_files[11].refcount=1;
    assert(nas_netlink_route_open(11,9000)==0);
    assert(nas_netlink_route_get_sndbuf(11,&sndbuf)==0 &&
           sndbuf==NAS_NETLINK_ROUTE_MAX_DATAGRAM);
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 &&
           rcvbuf==real_rx_bytes);
    assert(nas_netlink_route_bind(11,addr,12)==0);
    assert(nas_netlink_route_getsockname(11,name)==0);
    assert(rd32(name+4)==9000);
    puts("[NETLINK_ROUTE HOST PASS] single-snapshot IDs, address loss, and max eight-NIC RTM_GETADDR/RTM_GETLINK multipart, "
         "SO_SNDBUF/SO_RCVBUF quotas, SOCK_RAW port bind, MSG_PEEK/TRUNC, poll/release");
    return 0;
}
