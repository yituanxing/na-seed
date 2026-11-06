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
#include "../ports/lwip/socket_port.h"
#include "../kernel/drivers/port/net.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task;
/* Host-only stub: the standalone SOCK_DIAG fixture supplies these samples;
 * the ROUTE regression runs with an honest empty listener registry. */
static int diag_fixture_count;
static struct nas_lwip_tcp_listener diag_fixture[3];
int nas_lwip_tcp_listeners_snapshot(struct nas_lwip_tcp_listener *out,
                                    unsigned capacity) {
    if(diag_fixture_count<0)return diag_fixture_count;
    if((unsigned)diag_fixture_count>capacity)return -ENOBUFS;
    for(int i=0;i<diag_fixture_count;i++)out[i]=diag_fixture[i];
    return diag_fixture_count;
}
static bool full_snapshot,second_addressed=true,long_name_mode;
/* A live interface owns the tested attachment, not the Netlink provider.
 * Production attaches this object from tcpip_thread to the real loopif and
 * uses the same direct-output guard in IPv4/IPv6 TX callbacks. */
static struct nas_net_qdisc owned_loopback_qdisc;
unsigned nas_lwip_guest_qdisc_snapshot(
    struct nas_lwip_guest_qdisc out[NAS_LWIP_GUEST_IF_LIMIT]) {
    if(!out || !nas_net_qdisc_direct(&owned_loopback_qdisc,1U))return 0;
    out[0]=(struct nas_lwip_guest_qdisc){
        .ifindex=owned_loopback_qdisc.ifindex,.kind=owned_loopback_qdisc.kind
    };
    return 1;
}
/* Route statistics must share real NetDevice ownership, not the DHCP/IP
 * snapshot alone. This fixture emulates the pinned network-device port's
 * stable names and supplies deliberately nontrivial, >32-bit counters.
 * Production uses nas_netdev_get/stats_snapshot/put from the real registry. */
static struct nas_netdev stats_devices[NAS_NETDEV_LIMIT];
static char stats_names[NAS_NETDEV_LIMIT][5];
static unsigned stats_pin_count,stats_snapshot_count;
struct nas_netdev *nas_netdev_get(const char *name) {
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++)
        if(stats_devices[i].registered &&
           !strcmp(stats_devices[i].name,name)) {
            stats_devices[i].refs++;
            stats_pin_count++;
            return &stats_devices[i];
        }
    return NULL;
}
int nas_netdev_put(struct nas_netdev *device) {
    if(!device || !device->refs)return -EINVAL;
    device->refs--;
    stats_pin_count--;
    return 0;
}
int nas_netdev_stats_snapshot(const struct nas_netdev *device,
                              struct nas_netdev_stats *out) {
    if(!device || !device->registered || !out)return -ENODEV;
    *out=device->stats;
    stats_snapshot_count++;
    return 0;
}
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
        if(long_name_mode && i==1)
            memcpy(nic->name,"eth012345678901",16);
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
static uint64_t rd64(const uint8_t *p) {
    return rd32(p)|((uint64_t)rd32(p+4)<<32);
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
    const struct nas_netdev_stats *stats=NULL;
    if(index!=1) {
        const unsigned phy=(unsigned)(name[3]-'0');
        assert(phy<NAS_NETDEV_LIMIT);
        stats=&stats_devices[phy].stats;
    }
    assert(rd32(reply)==mtu_off+8U+(stats?304U:0U) &&
           rd16(reply+4)==16);
    assert(rd16(reply+6)==2 && rd32(reply+8)==seq && rd32(reply+12)==port);
    assert(reply[16]==0 && rd16(reply+18)==type && rd32(reply+20)==index);
    assert(rd32(reply+24)==flags && rd32(reply+28)==0);
    assert(rd16(reply+32)==attr && rd16(reply+34)==3);
    assert(!memcmp(reply+36,name,n));
    assert(rd16(reply+mtu_off)==8 && rd16(reply+mtu_off+2)==4 &&
           rd32(reply+mtu_off+4)==mtu);
    if(stats) {
        const unsigned off64=mtu_off+8U,off32=off64+204U;
        assert(rd16(reply+off64)==204 && rd16(reply+off64+2)==23);
        assert(rd16(reply+off32)==100 && rd16(reply+off32+2)==7);
        const uint64_t values[8]={
            stats->rx_packets,stats->tx_packets,stats->rx_bytes,
            stats->tx_bytes,0,stats->tx_errors,stats->rx_dropped,0
        };
        for(unsigned i=0;i<8;i++) {
            assert(rd64(reply+off64+4U+i*8U)==values[i]);
            assert(rd32(reply+off32+4U+i*4U)==(uint32_t)values[i]);
        }
        /* Optional fields have no actual device data and must not receive
         * fictitious positive packet, collision or error counts. */
        for(unsigned i=8;i<25;i++)
            assert(rd64(reply+off64+4U+i*8U)==0);
        for(unsigned i=8;i<24;i++)
            assert(rd32(reply+off32+4U+i*4U)==0);
    }
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
    for(unsigned i=0;i<NAS_NETDEV_LIMIT;i++) {
        stats_names[i][0]='e';stats_names[i][1]='t';
        stats_names[i][2]='h';
        stats_names[i][3]=(char)('0'+i);stats_names[i][4]=0;
        stats_devices[i]=(struct nas_netdev){
            .name=stats_names[i],.registered=true,
            .stats={.rx_packets=UINT64_C(0x100000002)+i,
                    .tx_packets=17+i,
                    .rx_bytes=UINT64_C(0x200000003)+i*40U,
                    .tx_bytes=2400+i,
                    .rx_dropped=4+i,.tx_errors=2+i}
        };
    }
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
    const uint32_t real_rx_bytes=(NAS_LWIP_GUEST_IF_LIMIT+1U)*384U;
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 &&
           rcvbuf==real_rx_bytes);
    assert(nas_netlink_route_get_rcvbuf(11,NULL)==-EINVAL);
    assert(nas_netlink_route_set_rcvbuf(11,1)==0);
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 && rcvbuf==384);
    uint8_t addr[12],name[12],request[20],reply[384],source[12];
    nl_sockaddr(addr,0,0x550); /* dnsmasq v2.91 multicast groups */
    assert(nas_netlink_route_bind(11,addr,12)==-EPERM);
    nl_sockaddr(addr,0,0);
    assert(nas_netlink_route_bind(11,addr,12)==0);
    assert(nas_netlink_route_bind(11,addr,12)==-EINVAL);
    getaddr(request,2,99);
    assert(nas_netlink_route_sendto(11,request,sizeof(request),addr,12)==
           -EAGAIN); /* four records cannot fit one 384B RCVBUF */
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
    assert(nas_netlink_route_get_rcvbuf(11,&rcvbuf)==0 && rcvbuf==384);
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

    /* RTM_GETLINK reports both 64/32-bit genuine NIC counters, captured
     * with a real registry pin; loopback has no false statistics. */
    uint8_t link_request[32];
    getlink(link_request,0,201);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==48);
    expect_newlink(reply,4142,201,1,772,0x10049,"lo",65536);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==356);
    expect_newlink(reply,4142,201,2,1,0x10041,"eth0",1500);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==356);
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
                                        NULL,NULL)==(i?356:48));
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
                                        NULL,NULL)==(i?356:48));
        char name[5]="eth0";
        if(i)name[3]=(char)('0'+i-1U);
        expect_newlink(reply,4142,205,i?100U+i:1U,i?1U:772U,
                       i?0x10041U:0x10049U,i?name:"lo",i?1500U:65536U);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(!stats_pin_count && stats_snapshot_count>0);
    full_snapshot=false;
    /* The IFNAMSIZ-1 longest legal name drives the real maximum wire
     * payload: 16+16+20+8+204+100=364 B. Its receive must fit and leave
     * the adjacent destination canary untouched. */
    long_name_mode=true;
    stats_devices[0].name="eth012345678901";
    getlink(link_request,0,207);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==48);
    struct {uint8_t bytes[364];uint32_t canary;} exact_packet={
        .canary=UINT32_C(0xfeedbabe)};
    assert(nas_netlink_route_recvmsg(11,exact_packet.bytes,
                                    sizeof(exact_packet.bytes),0,NULL,
                                    NULL,NULL)==364);
    assert(exact_packet.canary==UINT32_C(0xfeedbabe));
    expect_newlink(exact_packet.bytes,4142,207,2,1,0x10041,
                   "eth012345678901",1500);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==356);
    expect_newlink(reply,4142,207,3,1,0x10041,"eth1",1500);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    long_name_mode=false;
    stats_devices[0].name=stats_names[0];
    /* A concurrent NIC withdrawal while collecting the one-shot source
     * data fails closed BEFORE any multipart reply is published. */
    stats_devices[1].registered=false;
    getlink(link_request,0,208);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==-ENODEV);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    stats_devices[1].registered=true;
    getaddr(request,2,104);
    wr32(request,21);
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EINVAL);
    wr32(request,20);wr16(request+4,26); /* RTM_GETROUTE unsupported */
    assert(nas_netlink_route_sendto(11,request,20,addr,12)==-EOPNOTSUPP);
    /* Real libnl/bmon requests a link cache and then an OPTIONAL qdisc
     * cache. Linux reports unsupported GETQDISC through NLMSG_ERROR, not
     * by failing sendmsg(): otherwise libnl's send seq advances but its
     * receive seq does not, so the NEXT legitimate link dump is rejected
     * as "Message sequence number mismatch". No qdisc is invented here. */
    uint8_t qdisc[36]={0};
    wr32(qdisc,36);wr16(qdisc+4,38); /* RTM_GETQDISC */
    wr16(qdisc+6,0x305);wr32(qdisc+8,209);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==36);
    assert(ops->poll_description(11,1)&1);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,source,
                                    &source_len,&msg_flags)==36);
    assert(rd32(reply)==36 && rd16(reply+4)==2); /* NLMSG_ERROR */
    assert(rd16(reply+6)==0 && rd32(reply+8)==209 &&
           rd32(reply+12)==4142 && rd32(reply+16)==(uint32_t)-EOPNOTSUPP);
    assert(!memcmp(reply+20,qdisc,16) && rd32(source+4)==0);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* Seeing an interface called lo is insufficient; before the upstream
     * core attaches its actual output policy, there is no owned qdisc. */
    wr32(qdisc+20,1); /* tcmsg.tcm_ifindex = actual lo interface */
    assert(!nas_net_qdisc_direct(&owned_loopback_qdisc,1));
    wr32(qdisc+8,213);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==36);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==36);
    assert(rd16(reply+4)==2 && rd32(reply+8)==213 &&
           rd32(reply+16)==(uint32_t)-EOPNOTSUPP);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* Actual attachment changes direct TX policy and makes the qdisc
     * visible. No other link obtains a discipline from this transition. */
    assert(!nas_net_qdisc_attach_noqueue(&owned_loopback_qdisc,0));
    assert(nas_net_qdisc_attach_noqueue(&owned_loopback_qdisc,1));
    assert(!nas_net_qdisc_attach_noqueue(&owned_loopback_qdisc,1));
    assert(nas_net_qdisc_direct(&owned_loopback_qdisc,1));
    assert(!nas_net_qdisc_direct(&owned_loopback_qdisc,2));
    wr32(qdisc+8,211);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==36);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==48);
    assert(rd32(reply)==48 && rd16(reply+4)==36 &&
           rd32(reply+8)==211 && rd32(reply+12)==4142);
    assert(rd32(reply+20)==1 && rd32(reply+28)==UINT32_MAX);
    assert(rd16(reply+36)==12 && rd16(reply+38)==1 &&
           !memcmp(reply+40,"noqueue",8));
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==211);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    wr32(qdisc+20,2); /* real eth0 exists but has no qdisc model */
    wr32(qdisc+8,212);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==36);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==36);
    assert(rd16(reply+4)==2 && rd32(reply+8)==212 &&
           rd32(reply+16)==(uint32_t)-EOPNOTSUPP);
    /* Removing the upstream loopif must retire its qdisc first. An old
     * stale ifindex must never retain a fabricated noqueue reply. */
    nas_net_qdisc_detach(&owned_loopback_qdisc);
    assert(!nas_net_qdisc_direct(&owned_loopback_qdisc,1));
    wr32(qdisc+20,1);wr32(qdisc+8,214);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==36);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==36);
    assert(rd16(reply+4)==2 && rd32(reply+8)==214 &&
           rd32(reply+16)==(uint32_t)-EOPNOTSUPP);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
    /* Next cache dump receives the independent, new sequence normally. */
    getlink(link_request,0,210);
    assert(nas_netlink_route_sendto(11,link_request,sizeof(link_request),
                                   addr,12)==32);
    for(unsigned i=0;i<3;i++) {
        assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==(i?356:48));
        assert(rd16(reply+4)==16 && rd32(reply+8)==210 &&
               rd32(reply+12)==4142);
    }
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==20);
    assert(rd16(reply+4)==3 && rd32(reply+8)==210);
    wr32(qdisc,35);
    assert(nas_netlink_route_sendto(11,qdisc,sizeof(qdisc),addr,12)==-EINVAL);
    wr32(qdisc,36);
    assert(nas_netlink_route_recvmsg(11,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);
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
    puts("[NETLINK_ROUTE HOST PASS] true 64/32-bit NIC statistics, pinned all-or-nothing snapshots, single-snapshot IDs, address loss, eight-NIC multipart, owned lo qdisc attach/TX policy/detach + unsupported GETQDISC sequencing, "
         "SO_SNDBUF/SO_RCVBUF quotas, SOCK_RAW port bind, MSG_PEEK/TRUNC, poll/release");
    return 0;
}
