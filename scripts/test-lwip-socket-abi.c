/* Host Linux-socket ABI proof. Import the real M106 usercopy/FD fixture and
 * real syscall sources, then exercise AF_UNIX isolation and opt-in AF_INET
 * selection against a deliberately controlled asynchronous File provider.
 * The separate test-lwip-socket-port.c executes upstream lwIP TCP/UDP; this
 * fixture tests syscall parsing/publication, not protocol algorithms. */
#define main m106_prior_main
#include "test-m106-usercopy-syscalls.c"
#undef main

/* These legacy backend identities are enough to prove that the actual
 * syscall adapter routes families correctly; full AF_UNIX/loopback protocol
 * behavior is tested separately by M107 and the existing guest suite. */
static bool abi_fake_ipv4[NAS_OPEN_FILE_LIMIT];
void nas_unix_socket_init(unsigned slot){assert(slot<NAS_OPEN_FILE_LIMIT);}
void nas_ipv4_socket_init(unsigned slot){
    assert(slot<NAS_OPEN_FILE_LIMIT);
    abi_fake_ipv4[slot]=true;
}
bool nas_ipv4_socket_active(unsigned slot){
    return slot<NAS_OPEN_FILE_LIMIT && abi_fake_ipv4[slot];
}
#ifdef NAS_ENABLE_LWIP
int nas_ipv4_socket_bind(unsigned slot,struct nas_ipv4_endpoint ep) {
    (void)slot;(void)ep;assert(!"lwIP bind fell into old loopback");return -EIO;
}
int nas_ipv4_socket_connect(unsigned slot,struct nas_ipv4_endpoint ep) {
    (void)slot;(void)ep;assert(!"lwIP connect fell into old loopback");return -EIO;
}
int nas_ipv4_socket_listen(unsigned slot,unsigned backlog) {
    (void)slot;(void)backlog;assert(!"lwIP listen fell into old loopback");return -EIO;
}
int nas_ipv4_socket_name(unsigned slot,bool peer,struct nas_ipv4_endpoint *out) {
    (void)slot;(void)peer;(void)out;
    assert(!"lwIP name fell into old loopback");return -EIO;
}
int nas_ipv4_socket_tcp_state(unsigned slot,uint8_t *state) {
    (void)slot;(void)state;assert(!"lwIP getsockopt fell into fake TCP");return -EIO;
}
int nas_ipv4_socket_reuseaddr(unsigned slot,bool set,bool *value) {
    (void)slot;(void)set;(void)value;assert(!"lwIP reuseaddr fell into fake TCP");return -EIO;
}
bool nas_ipv4_socket_listening(unsigned slot) {
    (void)slot;assert(!"lwIP listening fell into fake TCP");return false;
}
#endif

#ifdef NAS_ENABLE_LWIP
#include "../ports/lwip/socket_port.h"
static bool abi_live[NAS_OPEN_FILE_LIMIT];
static enum nas_lwip_bridge_type abi_type[NAS_OPEN_FILE_LIMIT];
static struct nas_lwip_bridge_address abi_bound[NAS_OPEN_FILE_LIMIT];
static struct nas_lwip_bridge_address abi_peer[NAS_OPEN_FILE_LIMIT];
static bool abi_listening[NAS_OPEN_FILE_LIMIT],abi_control[NAS_OPEN_FILE_LIMIT];
static unsigned abi_pending_checks[NAS_OPEN_FILE_LIMIT];
static int abi_control_status[NAS_OPEN_FILE_LIMIT];
static int abi_so_error[NAS_OPEN_FILE_LIMIT];
static unsigned abi_opens,abi_releases,abi_binds,abi_connects,abi_listens;
static bool abi_name_peer[NAS_OPEN_FILE_LIMIT];
static bool abi_reuse_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_broadcast_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_recverr_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_nodelay_enabled[NAS_OPEN_FILE_LIMIT];
static uint8_t abi_bound_ifindex[NAS_OPEN_FILE_LIMIT];
static uint8_t abi_pending_ifindex[NAS_OPEN_FILE_LIMIT];
static unsigned abi_bind_if_calls;
static unsigned abi_broadcasts;
static unsigned abi_shutdown_how[NAS_OPEN_FILE_LIMIT];
static unsigned abi_getnames,abi_reuses,abi_shutdowns;
static uint8_t abi_rx_length[NAS_OPEN_FILE_LIMIT];
static uint8_t abi_rx_bytes[NAS_OPEN_FILE_LIMIT][16];
static uint8_t abi_udp_last_send[NAS_LWIP_BRIDGE_DATA];
static unsigned abi_udp_send_count,abi_udp_sent_length;
static struct nas_lwip_bridge_address abi_udp_destination;

int64_t nas_lwip_socket_sendto(unsigned slot,const uint8_t *data,
                               uint16_t length,
                               struct nas_lwip_bridge_address destination) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(length>NAS_LWIP_BRIDGE_DATA)return -EMSGSIZE;
    assert(data && length);
    memcpy(abi_udp_last_send,data,length);
    abi_udp_sent_length=length;abi_udp_destination=destination;
    abi_udp_send_count++;
    return length;
}
int64_t nas_lwip_socket_recvfrom(unsigned slot,uint8_t *bytes,
                                 uint16_t capacity,
                                 struct nas_lwip_bridge_address *source,
                                 bool *truncated) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(!abi_rx_length[slot])return -EAGAIN;
    unsigned length=abi_rx_length[slot];
    unsigned n=length<capacity?length:capacity;
    if(n)memcpy(bytes,abi_rx_bytes[slot],n);
    if(source)*source=(struct nas_lwip_bridge_address){
        .ipv4=UINT32_C(0x7f000001),.port=38007
    };
    if(truncated)*truncated=n<length;
    abi_rx_length[slot]=0; /* no TCP-style UDP tail */
    return n;
}

static void abi_release(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && abi_live[slot]);
    abi_live[slot]=false;abi_releases++;
}
static uint64_t abi_read_plan(unsigned slot,uint64_t count) {
    assert(nas_lwip_socket_is_slot(slot));
    if(!abi_rx_length[slot])return (uint64_t)-EAGAIN;
    return count<abi_rx_length[slot]?count:abi_rx_length[slot];
}
static uint64_t abi_read(unsigned slot,uint8_t *out,uint64_t count) {
    assert(nas_lwip_socket_is_slot(slot) && count<=abi_rx_length[slot]);
    memcpy(out,abi_rx_bytes[slot],(size_t)count);
    abi_rx_length[slot]-=(uint8_t)count;
    memmove(abi_rx_bytes[slot],abi_rx_bytes[slot]+count,abi_rx_length[slot]);
    return count;
}
static const struct nas_file_ops abi_ops={.release=abi_release,
    .read_plan=abi_read_plan,.read=abi_read};
const struct nas_file_ops *nas_lwip_socket_port_ops(void){return &abi_ops;}
int nas_lwip_socket_open(unsigned slot,enum nas_lwip_bridge_type type) {
    assert(slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount);
    assert(!abi_live[slot]);
    abi_live[slot]=true;abi_type[slot]=type;abi_opens++;
    return 0;
}
int nas_lwip_socket_open_ready(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && abi_live[slot] ? 1:-EBADF;
}
bool nas_lwip_socket_is_slot(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && abi_live[slot];
}
int nas_lwip_socket_type(unsigned slot) {
    return nas_lwip_socket_is_slot(slot)? (int)abi_type[slot]:-EBADF;
}
int nas_lwip_socket_buffer_capacity(unsigned slot,bool sending) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_TCP)return -ENOPROTOOPT;
    return sending?1072:2144; /* pinned lwIP defaults, MSS=536 */
}
bool nas_lwip_socket_is_listening(unsigned slot) {
    return nas_lwip_socket_is_slot(slot) && abi_listening[slot];
}
int nas_lwip_socket_peer_snapshot(unsigned slot,
                                  struct nas_lwip_bridge_address *out) {
    if(!nas_lwip_socket_is_slot(slot) || !out)return -EBADF;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    if(!abi_peer[slot].port)return -ENOTCONN;
    *out=abi_peer[slot];
    return 0;
}
int nas_lwip_socket_port_set_peer(unsigned slot,
                                  struct nas_lwip_bridge_address peer) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_peer[slot]=peer;
    return 0;
}
int nas_lwip_socket_port_take_error(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    int error=abi_so_error[slot];abi_so_error[slot]=0;return error;
}
int nas_lwip_socket_bind(unsigned slot,struct nas_lwip_bridge_address address) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_bound[slot]=address;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_binds++;return 0;
}
int nas_lwip_socket_connect(unsigned slot,struct nas_lwip_bridge_address address) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_peer[slot]=address;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_connects++;return 0;
}
int nas_lwip_socket_listen(unsigned slot,unsigned backlog) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    assert(backlog<=16U);
    abi_listening[slot]=true;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_listens++;return 0;
}
int nas_lwip_socket_getname_begin(unsigned slot,bool peer) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_name_peer[slot]=peer;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_getnames++;return 0;
}
int nas_lwip_socket_getname_complete(unsigned slot,
                                      struct nas_lwip_bridge_address *address) {
    if(!nas_lwip_socket_is_slot(slot) || !address)return -EBADF;
    if(!abi_control[slot] || abi_pending_checks[slot])return 0;
    *address=abi_name_peer[slot]?abi_peer[slot]:abi_bound[slot];
    abi_control[slot]=false;
    return 1;
}
int nas_lwip_socket_shutdown(unsigned slot,unsigned how) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_shutdown_how[slot]=how;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_shutdowns++;return 0;
}
int nas_lwip_socket_setreuseaddr(unsigned slot,bool enabled) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    abi_reuse_enabled[slot]=enabled;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_reuses++;return 0;
}
int nas_lwip_socket_setbroadcast(unsigned slot,bool enabled) {
    if(!nas_lwip_socket_is_slot(slot) ||
       abi_type[slot]!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    abi_broadcast_enabled[slot]=enabled;abi_control[slot]=true;
    abi_pending_checks[slot]=1;abi_broadcasts++;return 0;
}
int nas_lwip_guest_ifindex(const char *device_name) {
    return device_name && !strcmp(device_name,"eth0") ? 2:-ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,
                         char name[NAS_LWIP_DEVICE_NAME_MAX]) {
    if(index!=2 || !name)return -ENODEV;
    memcpy(name,"eth0",5);
    return 0;
}
int nas_lwip_socket_bind_if(unsigned slot,unsigned index) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(index!=2)return -ENODEV;
    abi_pending_ifindex[slot]=(uint8_t)index;
    abi_control[slot]=true;abi_pending_checks[slot]=1;
    abi_bind_if_calls++;
    return 0;
}
int nas_lwip_socket_bound_ifindex(unsigned slot) {
    return nas_lwip_socket_is_slot(slot)?abi_bound_ifindex[slot]:-EBADF;
}
int nas_lwip_socket_tcp_nodelay(unsigned slot,bool write,bool *enabled) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!enabled)return -EINVAL;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    if(write) {
        abi_nodelay_enabled[slot]=*enabled;abi_control[slot]=true;
        abi_pending_checks[slot]=1;
    } else *enabled=abi_nodelay_enabled[slot];
    return 0;
}
int nas_lwip_socket_ip_recverr(unsigned slot,bool write,bool *enabled) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!enabled)return -EINVAL;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(write)abi_recverr_enabled[slot]=*enabled;
    else *enabled=abi_recverr_enabled[slot];
    return 0;
}
int nas_lwip_socket_control_ready(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot) || !abi_control[slot])return 0;
    if(abi_pending_checks[slot]) {abi_pending_checks[slot]--;return 0;}
    return 1;
}
int nas_lwip_socket_control_finish(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!abi_control[slot])return -EINPROGRESS;
    if(abi_pending_ifindex[slot] && !abi_control_status[slot])
        abi_bound_ifindex[slot]=abi_pending_ifindex[slot];
    abi_pending_ifindex[slot]=0;
    abi_control[slot]=false;return 0;
}
int nas_lwip_socket_port_complete(unsigned slot,
                                  struct nas_lwip_bridge_result *out) {
    if(!out || !nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!abi_control[slot] || abi_pending_checks[slot])return 0;
    out->status=nas_lwip_socket_control_finish(slot);
    if(!out->status)out->status=abi_control_status[slot];
    abi_control_status[slot]=0;
    return 1;
}
#endif

#ifdef NAS_ENABLE_LWIP
/* Keep host copies in mapped guest pages: native pointers are deliberately
 * never passed to the real syscall adapter. */
static uint64_t abi_sockaddr(uint16_t port,uint32_t ip) {
    const uint64_t addr=TEST_BASE+700;
    uint8_t bytes[16]={2,0,(uint8_t)(port>>8),(uint8_t)port,
                       (uint8_t)(ip>>24),(uint8_t)(ip>>16),
                       (uint8_t)(ip>>8),(uint8_t)ip};
    memcpy(user_ptr(addr),bytes,sizeof(bytes));
    return addr;
}
#endif

int main(void) {
    /* Run all original AF_UNIX socketpair and usercopy regressions first. */
    assert(m106_prior_main()==0);
    memset(&guest_files.table,0,sizeof(guest_files.table));
    nas_fd_system_init();
    reset_mapping();

    /* Linux errno parity for every socket syscall on a valid non-socket FD.
     * Alpine Ncat calls recvfrom(0) when its stdin is a shell pipeline: a
     * valid pipe must yield ENOTSOCK, not EBADF (which means closed). */
    /* Model the existing pipe descriptors directly: the prior M106 suite
     * exhausted the fixture's finite pipe-object pool. The real production
     * FD kind/table and OpenFile publication code are still used here. */
    unsigned input_fd=0,output_fd=1;
    assert(nas_fd_install_new(&guest_task,input_fd,TEST_OFD_START,
                              FD_PIPE_R,0,false));
    assert(nas_fd_install_new(&guest_task,output_fd,TEST_OFD_START+1,
                              FD_PIPE_W,1,false));
    assert(guest_task.fds->entries[input_fd].kind==FD_PIPE_R);
    const enum nas_syscall_op socket_fd_ops[]={
        NAS_SC_bind,NAS_SC_listen,NAS_SC_connect,NAS_SC_accept,
        NAS_SC_accept4,NAS_SC_getsockname,NAS_SC_getpeername,
        NAS_SC_getsockopt,NAS_SC_setsockopt,NAS_SC_sendmsg,
        NAS_SC_recvmsg,NAS_SC_sendto,NAS_SC_recvfrom,NAS_SC_shutdown
    };
    for(unsigned i=0;i<sizeof(socket_fd_ops)/sizeof(socket_fd_ops[0]);i++) {
        assert(socket_fd_error(socket_fd_ops[i],input_fd)==-ENOTSOCK);
        assert(socket_fd_error(socket_fd_ops[i],output_fd)==-ENOTSOCK);
        assert(socket_fd_error(socket_fd_ops[i],NAS_FD_LIMIT)==-EBADF);
    }
    assert(nas_fd_close(&guest_task,input_fd)==TEST_OFD_START);
    assert(nas_fd_close(&guest_task,output_fd)==TEST_OFD_START+1);
    assert(socket_fd_error(NAS_SC_recvfrom,input_fd)==-EBADF);

    /* AF_UNIX remains the same File provider when native lwIP is enabled. */
    uint64_t unix_fd_number=sys_socket(1,1|0x80000U,0);
    assert(unix_fd_number<NAS_FD_LIMIT);
    assert(guest_task.fds->entries[unix_fd_number].cloexec);
    assert(unix_fd(unix_fd_number));
#ifdef NAS_ENABLE_LWIP
    unsigned unix_slot=guest_task.fds->entries[unix_fd_number].slot;
    assert(!nas_lwip_socket_is_slot(unix_slot));
    assert(abi_opens==0);
    assert((int64_t)sys_socket(1,2,0)==-ESOCKTNOSUPPORT);

    /* Real syscall adapter chooses official-lwIP FileOps, preserving one
     * ordinary OpenFile lifetime and the caller's SOCK_NONBLOCK/CLOEXEC. */
    uint64_t tcp_fd=sys_socket(2,1U|0x800U|0x80000U,6);
    assert(tcp_fd<NAS_FD_LIMIT && tcp_fd!=unix_fd_number);
    unsigned tcp_slot=guest_task.fds->entries[tcp_fd].slot;
    assert(abi_live[tcp_slot] && abi_type[tcp_slot]==NAS_LWIP_BRIDGE_TCP);
    assert(nas_open_files[tcp_slot].ops==&abi_ops);
    assert(nas_open_files[tcp_slot].status_flags&0x800U);
    assert(guest_task.fds->entries[tcp_fd].cloexec);
    assert(!unix_fd(tcp_fd) && lwip_fd(tcp_fd) && inet_fd(tcp_fd));
    assert(socket_fd_error(NAS_SC_recvfrom,tcp_fd)==0);

    uint64_t udp_fd=sys_socket(2,2,17);
    assert(udp_fd<NAS_FD_LIMIT && udp_fd!=tcp_fd);
    unsigned udp_slot=guest_task.fds->entries[udp_fd].slot;
    assert(abi_live[udp_slot] && abi_type[udp_slot]==NAS_LWIP_BRIDGE_UDP);
    assert(nas_open_files[udp_slot].ops==&abi_ops && abi_opens==2);
    assert((int64_t)sys_socket(2,2,6)==-EPROTONOSUPPORT);
    assert((int64_t)sys_socket(2,1,17)==-EPROTONOSUPPORT);
    assert((int64_t)sys_socket(3,1,0)==-EAFNOSUPPORT);

    uint64_t sa=abi_sockaddr(34567,UINT32_C(0x7f000001));
    assert((int64_t)sys_ipv4_bind(tcp_fd,sa,15)==-EINVAL);
    assert((int64_t)sys_ipv4_bind(tcp_fd,sa,16)==-EAGAIN);
    assert(abi_binds==1 && abi_bound[tcp_slot].port==34567);
    assert(abi_bound[tcp_slot].ipv4==UINT32_C(0x7f000001));
    assert(!nas_fs_socket_control_ready(tcp_slot)); /* worker not finished */
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert((int64_t)sys_ipv4_listen(tcp_fd,32)==-EAGAIN);
    assert(abi_listens==1 && abi_listening[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);

    /* SOCK_NONBLOCK connect returns EINPROGRESS, not a fabricated success;
     * asynchronous completion is consumed once through the original slot. */
    assert((int64_t)sys_ipv4_connect(tcp_fd,sa,16)==-EINPROGRESS);
    assert(abi_connects==1 && abi_peer[tcp_slot].port==34567);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);

    uint64_t optaddr=TEST_BASE+800,optlen=TEST_BASE+900;
    uint32_t capacity=4;
    memcpy(user_ptr(optlen),&capacity,4);
    assert(sys_ipv4_getsockopt(udp_fd,1,3,optaddr,optlen)==0); /* SO_TYPE */
    uint32_t value;
    memcpy(&value,user_ptr(optaddr),4);assert(value==2);
    assert(sys_ipv4_getsockopt(udp_fd,1,38,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==17);
    assert(sys_ipv4_getsockopt(tcp_fd,1,39,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==2);
    /* The native TCP buffer introspection is derived from pinned official
     * lwIP configuration, not a fabricated Linux-sized window. */
    assert(sys_ipv4_getsockopt(tcp_fd,1,7,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==1072);
    assert(sys_ipv4_getsockopt(tcp_fd,1,8,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==2144);
    assert((int64_t)sys_ipv4_getsockopt(udp_fd,1,7,optaddr,optlen)==-ENOPROTOOPT);
    assert(sys_ipv4_getsockopt(tcp_fd,1,30,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==1);
    abi_so_error[tcp_slot]=-ECONNREFUSED;
    assert(sys_ipv4_getsockopt(tcp_fd,1,4,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==ECONNREFUSED);
    assert(sys_ipv4_getsockopt(tcp_fd,1,4,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==0);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,1,999,optaddr,optlen)==-ENOPROTOOPT);
    /* Nping's libnsock requests the Linux physical NIC name. A successful
     * setsockopt is asynchronous and changes the actual shared OpenFile only
     * after native worker success; invalid names never enqueue a fake bind. */
    *user_ptr(optaddr)=0;
    assert(sys_ipv4_setsockopt(udp_fd,1,25,optaddr,1)==0);
    assert(abi_bind_if_calls==0 && nas_lwip_socket_bound_ifindex(udp_slot)==0);
    memcpy(user_ptr(optaddr),"eth0",4); /* no NUL terminator is also legal */
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,4)==-EAGAIN);
    assert(abi_bind_if_calls==1 && abi_pending_ifindex[udp_slot]==2);
    assert(nas_lwip_socket_bound_ifindex(udp_slot)==0);
    assert(!nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_description(udp_slot)==0);
    assert(nas_lwip_socket_bound_ifindex(udp_slot)==2);
    capacity=16;memcpy(user_ptr(optlen),&capacity,4);
    assert(sys_ipv4_getsockopt(udp_fd,1,25,optaddr,optlen)==0);
    assert(!memcmp(user_ptr(optaddr),"eth0",5));
    memcpy(&capacity,user_ptr(optlen),4);assert(capacity==5);
    capacity=4;memcpy(user_ptr(optlen),&capacity,4);
    assert((int64_t)sys_ipv4_getsockopt(udp_fd,1,25,optaddr,optlen)==-EINVAL);
    assert((int64_t)sys_ipv4_getsockopt(udp_fd,1,25,0,optlen)==-EINVAL);
    memcpy(user_ptr(optaddr),"ghost0",7);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,7)==-ENODEV);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,0)==-EINVAL);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,17)==-EINVAL);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,0,5)==-EFAULT);
    memset(user_ptr(optaddr),'x',16);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,16)==-ENAMETOOLONG);
    *user_ptr(optaddr)=0;
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,25,optaddr,1)==-EOPNOTSUPP);
    assert(abi_bind_if_calls==1 && nas_lwip_socket_bound_ifindex(udp_slot)==2);
    capacity=4;memcpy(user_ptr(optlen),&capacity,4);
    /* BIND dig enables IP_RECVERR on its UDP OpenFile before emitting a
     * DNS question. It must be a real per-description option, readable
     * back without a phantom protocol queue. */
    value=1;
    memcpy(user_ptr(optaddr),&value,4);
    assert(sys_ipv4_setsockopt(udp_fd,0,11,optaddr,4)==0);
    assert(abi_recverr_enabled[udp_slot]);
    assert(sys_ipv4_getsockopt(udp_fd,0,11,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==1);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,0,11,optaddr,4)==-EOPNOTSUPP);
    value=0;
    memcpy(user_ptr(optaddr),&value,4);
    assert(sys_ipv4_setsockopt(udp_fd,0,11,optaddr,4)==0);
    assert(!abi_recverr_enabled[udp_slot]);

    /* GETNAME completion uses real syscall usercopy after native completion,
     * not a foreign Task's memory from the worker. Validate byte order and
     * reported Linux sockaddr size, including short output capacity. */
    uint32_t name_capacity=8;
    uint64_t nameaddr=TEST_BASE+1000,namelen=TEST_BASE+1100;
    memcpy(user_ptr(namelen),&name_capacity,4);
    assert((int64_t)sys_ipv4_name(tcp_fd,nameaddr,namelen,false)==-EAGAIN);
    assert(abi_getnames==1 && !abi_name_peer[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_name_description(tcp_slot,nameaddr,namelen,false)==0);
    uint32_t actual;
    memcpy(&actual,user_ptr(namelen),4);assert(actual==16);
    const uint8_t local_expected[8]={2,0,0x87,0x07,127,0,0,1};
    assert(!memcmp(user_ptr(nameaddr),local_expected,8));
    /* Preflight EFAULT must leave the worker without a newly queued query. */
    can_write[0]=false;
    assert((int64_t)sys_ipv4_name(tcp_fd,nameaddr,namelen,true)==-EFAULT);
    assert(abi_getnames==1);
    can_write[0]=true;
    name_capacity=16;memcpy(user_ptr(namelen),&name_capacity,4);
    assert((int64_t)sys_ipv4_name(tcp_fd,nameaddr,namelen,true)==-EAGAIN);
    assert(abi_getnames==2 && abi_name_peer[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_name_description(tcp_slot,nameaddr,namelen,true)==0);
    memcpy(&actual,user_ptr(namelen),4);assert(actual==16);
    assert(!memcmp(user_ptr(nameaddr),local_expected,8));
    /* A shared VM may invalidate the output AFTER the native GETNAME was
     * queued; EFAULT must still consume its completed worker command. */
    assert((int64_t)sys_ipv4_name(tcp_fd,nameaddr,namelen,true)==-EAGAIN);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    can_write[0]=false;
    assert((int64_t)nas_fs_socket_name_description(
        tcp_slot,nameaddr,namelen,true)==-EFAULT);
    can_write[0]=true;
    assert(!abi_control[tcp_slot]);

    /* Real native AF_INET TCP recvfrom can return source sockaddr, as Ncat
     * does even on a connected stream. It must retain buffer preflight and
     * use the existing FileOps read, not a simulated IPv4 packet provider. */
    const uint64_t rxbuf=TEST_BASE+1400,srcaddr=TEST_BASE+1500;
    const uint64_t srclen=TEST_BASE+1600;
    uint32_t source_capacity=5;
    memcpy(user_ptr(srclen),&source_capacity,4);
    memcpy(abi_rx_bytes[tcp_slot],"PONG",4);abi_rx_length[tcp_slot]=4;
    can_write[0]=false;
    assert((int64_t)sys_ipv4_recvfrom(
        tcp_fd,rxbuf,4,0,srcaddr,srclen)==-EFAULT);
    assert(abi_rx_length[tcp_slot]==4);
    can_write[0]=true;
    assert(sys_ipv4_recvfrom(tcp_fd,rxbuf,4,0,srcaddr,srclen)==4);
    assert(!memcmp(user_ptr(rxbuf),"PONG",4));
    assert(abi_rx_length[tcp_slot]==0);
    uint32_t source_actual=0;
    memcpy(&source_actual,user_ptr(srclen),4);assert(source_actual==16);
    const uint8_t source_expected[5]={2,0,0x87,0x07,127};
    assert(!memcmp(user_ptr(srcaddr),source_expected,5));
    assert((int64_t)sys_ipv4_recvfrom(tcp_fd,rxbuf,4,0,srcaddr,0)==-EFAULT);

    /* Addressed UDP uses one copied packet and the native peer metadata.
     * Multiple iovecs in sendmsg must never become multiple datagrams. */
    const uint64_t udp_target=abi_sockaddr(38007,UINT32_C(0x7f000001));
    const uint64_t udp_payload=TEST_BASE+1700;
    memcpy(user_ptr(udp_payload),"PING",4);
    assert(sys_ipv4_sendto(udp_fd,udp_payload,4,0,udp_target,16)==4);
    assert(abi_udp_send_count==1 && abi_udp_sent_length==4 &&
           !memcmp(abi_udp_last_send,"PING",4));
    assert(abi_udp_destination.port==38007 &&
           abi_udp_destination.ipv4==UINT32_C(0x7f000001));
    assert((int64_t)sys_ipv4_sendto(
        udp_fd,udp_payload,NAS_LWIP_BRIDGE_DATA+1,0,udp_target,16)==-EMSGSIZE);

    const uint64_t msgaddr=TEST_BASE+1800,iovaddr=TEST_BASE+1920;
    const struct nas_iovec udp_iov[2]={
        {.base=udp_payload,.length=2},
        {.base=udp_payload+2,.length=2}
    };
    memcpy(user_ptr(iovaddr),udp_iov,sizeof(udp_iov));
    struct nas_msghdr msg={.name=udp_target,.namelen=16,
                            .iov=iovaddr,.iovlen=2};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert(sys_ipv4_sendmsg(udp_fd,msgaddr,0)==4);
    assert(abi_udp_send_count==2 && abi_udp_sent_length==4 &&
           !memcmp(abi_udp_last_send,"PING",4));
    assert((int64_t)sys_ipv4_sendmsg(udp_fd,msgaddr,0x2ULL)==-EOPNOTSUPP);

    /* Ncat -u sets SO_BROADCAST before its nonblocking UDP connect. The
     * first queues a real PCB control operation; the second has no TCP
     * handshake and therefore cannot report a spurious EINPROGRESS. */
    uint32_t broadcast_on=1;
    memcpy(user_ptr(udp_payload),&broadcast_on,4);
    assert((int64_t)sys_ipv4_setsockopt(
        udp_fd,1,6,udp_payload,4)==-EAGAIN);
    assert(abi_broadcasts==1 && abi_broadcast_enabled[udp_slot]);
    assert(!nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_description(udp_slot)==0);
    assert(sys_ipv4_connect(udp_fd,udp_target,16)==0);
    assert(abi_connects==2 && abi_peer[udp_slot].port==38007);

    memcpy(abi_rx_bytes[udp_slot],"PONG",4);abi_rx_length[udp_slot]=4;
    uint32_t udp_source_capacity=8;
    memcpy(user_ptr(srclen),&udp_source_capacity,4);
    can_write[0]=false;
    assert((int64_t)sys_ipv4_recvfrom(
        udp_fd,rxbuf,4,0,srcaddr,srclen)==-EFAULT);
    assert(abi_rx_length[udp_slot]==4);
    can_write[0]=true;
    assert(sys_ipv4_recvfrom(udp_fd,rxbuf,4,0,srcaddr,srclen)==4);
    assert(!memcmp(user_ptr(rxbuf),"PONG",4));
    assert(abi_rx_length[udp_slot]==0);
    const uint8_t expected_udp_source[8]={2,0,0x94,0x77,127,0,0,1};
    assert(!memcmp(user_ptr(srcaddr),expected_udp_source,8));

    memcpy(abi_rx_bytes[udp_slot],"DATAGRAM",8);abi_rx_length[udp_slot]=8;
    const struct nas_iovec rx_iov[2]={
        {.base=rxbuf,.length=2},{.base=rxbuf+2,.length=3}
    };
    memcpy(user_ptr(iovaddr),rx_iov,sizeof(rx_iov));
    msg=(struct nas_msghdr){.name=srcaddr,.namelen=8,
                            .iov=iovaddr,.iovlen=2};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert(sys_ipv4_recvmsg(udp_fd,msgaddr,0)==5);
    assert(!memcmp(user_ptr(rxbuf),"DATAG",5));
    memcpy(&msg,user_ptr(msgaddr),sizeof(msg));
    assert(msg.namelen==16 && msg.flags==0x20U && msg.controllen==0);
    assert(abi_rx_length[udp_slot]==0);

    /* SO_REUSEADDR and SHUTDOWN both wait for a native control result.
     * Unsupported or invalid controls must not pretend to have succeeded. */
    uint32_t enabled=1;
    memcpy(user_ptr(optaddr),&enabled,4);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,1,2,optaddr,4)==-EAGAIN);
    assert(abi_reuses==1 && abi_reuse_enabled[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,6,1,optaddr,4)==-EAGAIN);
    assert(abi_nodelay_enabled[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert(sys_ipv4_getsockopt(tcp_fd,6,1,optaddr,optlen)==0);
    memcpy(&enabled,user_ptr(optaddr),4);assert(enabled==1);
    assert(abi_reuses==1);
    assert((int64_t)sys_socket_shutdown(tcp_fd,3)==-EINVAL);
    assert((int64_t)sys_socket_shutdown(tcp_fd,2)==-EAGAIN);
    assert(abi_shutdowns==1 && abi_shutdown_how[tcp_slot]==2);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    /* EINPROGRESS is a *real* possible completed native status. It cannot
     * be interpreted as the sentinel for a still-pending worker request. */
    assert((int64_t)sys_socket_shutdown(tcp_fd,1)==-EAGAIN);
    abi_control_status[tcp_slot]=-EINPROGRESS;
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert((int64_t)nas_fs_socket_control_description(tcp_slot)==-EINPROGRESS);
    assert(!abi_control[tcp_slot]);
    puts("[LWIP SOCKET ABI PASS] native AF_INET TCP/UDP OpenFile + user sockaddr,");
    puts("  async control/GETNAME/SHUTDOWN/REUSEADDR, AF_UNIX preserved");
#else
    assert((int64_t)sys_socket(2,2,0)==-ESOCKTNOSUPPORT);
    uint64_t legacy_fd=sys_socket(2,1,6);
    assert(legacy_fd<NAS_FD_LIMIT && inet_fd(legacy_fd));
    assert(!unix_fd(legacy_fd));
    puts("[LEGACY SOCKET ABI PASS] AF_UNIX and simulated AF_INET unchanged");
#endif
    return 0;
}
