/* Host ABI proof for Linux NETLINK_SOCK_DIAG/INET_DIAG TCP LISTEN. The
 * imported ROUTE fixture runs its own protocol-0 regression first; its only
 * fake state is a test-controlled snapshot of what socket_port emits from
 * actual native lwIP, never a production diagnostic record. */
#define main route_fixture_main
#include "test-netlink-route.c"
#undef main

static void diag_request(uint8_t request[72],uint8_t family,
                         uint32_t states,uint32_t seq) {
    memset(request,0,72);
    wr32(request,72);
    wr16(request+4,20); /* SOCK_DIAG_BY_FAMILY */
    wr16(request+6,0x301); /* NLM_F_REQUEST | NLM_F_DUMP */
    wr32(request+8,seq);
    request[16]=family;
    request[17]=6; /* IPPROTO_TCP */
    wr32(request+20,states);
    wr32(request+64,UINT32_MAX); /* INET_DIAG_NOCOOKIE */
    wr32(request+68,UINT32_MAX);
}

static void diag_done(unsigned slot,uint32_t seq) {
    uint8_t reply[96]={0};
    uint8_t source[12];
    uint32_t namelen=0,flags=0;
    assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,source,
                                    &namelen,&flags)==20);
    assert(rd32(reply)==20 && rd16(reply+4)==3 && rd16(reply+6)==2);
    assert(rd32(reply+8)==seq && rd32(reply+12)==0 && rd32(reply+16)==0);
    assert(namelen==12 && rd16(source)==16 && rd32(source+4)==0);
    assert(flags==0);
}

int main(void) {
    assert(route_fixture_main()==0);
    const struct nas_file_ops *ops=nas_netlink_route_ops();
    const unsigned slot=66,other=67;
    nas_open_files[slot].refcount=1;
    nas_open_files[other].refcount=1;
    assert(nas_netlink_diag_open(slot,7777)==0);
    assert(nas_netlink_route_open(other,7777)==0);
    assert(nas_netlink_route_protocol(slot)==4);
    assert(nas_netlink_route_protocol(other)==0);
    uint32_t capacity=0;
    assert(nas_netlink_route_get_rcvbuf(slot,&capacity)==0);
    assert(capacity==(NAS_LWIP_GUEST_IF_LIMIT+1U)*96);
    uint8_t sockaddr[12],name[12],req[72],reply[96],source[12];
    nl_sockaddr(sockaddr,0,0);
    assert(nas_netlink_route_bind(slot,sockaddr,12)==0);
    assert(nas_netlink_route_bind(other,sockaddr,12)==0);
    assert(nas_netlink_route_getsockname(slot,name)==0);
    assert(rd32(name+4)==7777); /* protocol-specific port namespace */

    /* An empty native listening set must not create a fabricated socket. */
    diag_request(req,2,1U<<10,100);
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==72);
    diag_done(slot,100);
    assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);

    /* The native LISTEN snapshot contains the actual bound endpoint and
     * actual committed backlog; ss must display exactly these bytes. */
    diag_fixture_count=1;
    diag_fixture[0]=(struct nas_lwip_tcp_listener){
        .slot=44,.local={.ipv4=UINT32_C(0x7f000001),.port=18085},
        .backlog=5
    };
    diag_request(req,2,1U<<10,101);
    assert(nas_netlink_route_sendto(slot,req,72,sockaddr,12)==72);
    assert(ops->poll_description(slot,1)&1);
    uint32_t namelen=0,flags=0;
    memset(reply,0,sizeof(reply));
    assert(nas_netlink_route_recvmsg(slot,reply,6,2|32,source,
                                    &namelen,&flags)==88);
    assert(flags==32 && namelen==12);
    assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,source,
                                    &namelen,&flags)==88);
    assert(flags==0 && rd32(reply)==88 && rd16(reply+4)==20 &&
           rd16(reply+6)==2 && rd32(reply+8)==101 && rd32(reply+12)==0);
    assert(reply[16]==2 && reply[17]==10);
    assert(reply[20]==(18085>>8) && reply[21]==(18085&255));
    const uint8_t loopback[4]={127,0,0,1};
    assert(!memcmp(reply+24,loopback,4));
    for(unsigned i=28;i<60;i++)assert(reply[i]==0);
    assert(rd32(reply+60)==0 && rd32(reply+64)==UINT32_MAX &&
           rd32(reply+68)==UINT32_MAX);
    assert(rd32(reply+72)==0 && rd32(reply+76)==5 &&
           rd32(reply+80)==0 && rd32(reply+84)==45);
    diag_done(slot,101);

    /* Actual iproute2 `ss -ltn` includes SS_CLOSE as well as SS_LISTEN.
     * Return the real listener, without inventing any CLOSE entry. */
    diag_request(req,2,(1U<<10)|(1U<<7),109);
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==72);
    assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==88);
    assert(rd32(reply+8)==109 && rd16(reply+4)==20 && reply[17]==10 &&
           reply[20]==(18085>>8) && reply[21]==(18085&255));
    diag_done(slot,109);

    /* No invented IPv6 PCB or partial filter support. */
    diag_request(req,10,1U<<10,102);
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==72);
    diag_done(slot,102);
    diag_request(req,2,1U<<1,103); /* ESTABLISHED not inventoried */
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==-EOPNOTSUPP);
    diag_request(req,2,1U<<10,104);
    req[24]=1; /* source-port selector unsupported */
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==-EOPNOTSUPP);
    diag_request(req,2,1U<<10,105);
    req[18]=1; /* requested extension unsupported */
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==-EOPNOTSUPP);
    diag_request(req,2,1U<<10,106);
    assert(nas_netlink_route_sendto(slot,req,71,NULL,0)==-EINVAL);

    /* Native listener observation may fail; a bogus empty dump is never
     * an acceptable substitute for missing endpoint/queue data. */
    diag_fixture_count=-EOPNOTSUPP;
    diag_request(req,2,1U<<10,107);
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==-EOPNOTSUPP);

    diag_fixture_count=3;
    for(int i=0;i<3;i++)diag_fixture[i]=(struct nas_lwip_tcp_listener){
        .slot=(unsigned)(50+i),.local={.ipv4=0,.port=(uint16_t)(8080+i)},
        .backlog=2
    };
    diag_request(req,2,1U<<10,108);
    assert(nas_netlink_route_set_rcvbuf(slot,96)==0); /* 192B total */
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==-EAGAIN);
    assert(!(ops->poll_description(slot,1)&1));
    assert(nas_netlink_route_set_rcvbuf(slot,UINT32_MAX)==0);
    assert(nas_netlink_route_sendto(slot,req,72,NULL,0)==72);
    for(unsigned i=0;i<3;i++) {
        assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,NULL,
                                        NULL,NULL)==88);
        assert(rd16(reply+4)==20 && rd32(reply+84)==51+i);
    }
    diag_done(slot,108);
    assert(nas_netlink_route_recvmsg(slot,reply,sizeof(reply),0,NULL,
                                    NULL,NULL)==-EAGAIN);

    nas_open_files[slot].refcount=0;
    ops->release(slot);
    nas_open_files[other].refcount=0;
    ops->release(other);
    assert(!nas_netlink_route_is_slot(slot));
    puts("[NETLINK_SOCK_DIAG HOST PASS] authentic IPv4 TCP LISTEN multipart, empty/IPv6, filters, sequence, quotas, peek/trunc, protocol isolation and release");
    return 0;
}
