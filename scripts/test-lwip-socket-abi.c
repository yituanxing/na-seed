/* Host Linux-socket ABI proof. Import the real M106 usercopy/FD fixture and
 * real syscall sources, then exercise AF_UNIX isolation and native AF_INET
 * selection against a deliberately controlled asynchronous File provider.
 * The separate test-lwip-socket-port.c executes upstream lwIP TCP/UDP; this
 * fixture tests syscall parsing/publication, not protocol algorithms. */
#define main m106_prior_main
#include "test-m106-usercopy-syscalls.c"
#undef main
#include "../kernel/fs/netlink_route.h"
#include "../kernel/fs/netlink_filter.h"
#include "../kernel/fs/netlink_generic.h"

/* Only the single upstream-lwIP AF_INET provider exists. The fake provider
 * below tests File publication/ABI, not another TCP/IP implementation. */
void nas_unix_socket_init(unsigned slot){assert(slot<NAS_OPEN_FILE_LIMIT);}
/* This fixture imports the real generic open syscall adapter, but exercises
 * network descriptors only. Opening a VT must remain observable as an
 * unexpected test path instead of leaving its production hook unresolved. */
void nas_vt_open_session(uint64_t flags) {
    (void)flags;
    assert(!"unexpected virtual-terminal open in socket ABI fixture");
}
void nas_pty_open_session(unsigned slot,uint64_t flags) {
    /* The imported M106 fixture explicitly exercises a mocked PTY open.
     * Session ownership is separately verified against the real backend
     * in check-m188-pty.sh; this socket test has no real PTY state. */
    (void)slot;(void)flags;
}

#include "../ports/lwip/socket_port.h"
/* This ABI-only fixture does not run native TCP listeners; the real
 * socket_port.c implementation is exercised by its separate host test. */
int nas_lwip_tcp_listeners_snapshot(struct nas_lwip_tcp_listener *out,
                                    unsigned capacity) {
    (void)out;(void)capacity;return 0;
}
/* SIOCGIFCONF now exercises the real generic ioctl dispatcher. Its other
 * filesystem/PTY branches are unrelated to this network fixture and must
 * not silently succeed if an unexpected branch becomes reachable. */
uint64_t nas_vfs_node_size(const struct nas_fs_node *node) {
    (void)node;assert(!"unexpected VFS ioctl");return 0;
}
int nas_vfs_fiemap_next(const struct nas_fs_node *node,uint64_t start,
                        uint64_t end,uint64_t *logical,uint64_t *length,
                        bool *last) {
    (void)node;(void)start;(void)end;(void)logical;(void)length;(void)last;
    assert(!"unexpected FIEMAP ioctl");return -EIO;
}
static bool abi_live[NAS_OPEN_FILE_LIMIT];
static bool abi_retiring_bridge[NAS_OPEN_FILE_LIMIT];
bool nas_lwip_socket_slot_reusable(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && !abi_live[slot] &&
           !abi_retiring_bridge[slot];
}
static enum nas_lwip_bridge_type abi_type[NAS_OPEN_FILE_LIMIT];
static struct nas_lwip_bridge_address abi_bound[NAS_OPEN_FILE_LIMIT];
static struct nas_lwip_bridge_address abi_peer[NAS_OPEN_FILE_LIMIT];
static bool abi_listening[NAS_OPEN_FILE_LIMIT],abi_control[NAS_OPEN_FILE_LIMIT];
static bool abi_tcp_info_pending[NAS_OPEN_FILE_LIMIT];
static unsigned abi_pending_checks[NAS_OPEN_FILE_LIMIT];
static int abi_control_status[NAS_OPEN_FILE_LIMIT];
static int abi_so_error[NAS_OPEN_FILE_LIMIT];
static unsigned abi_opens,abi_releases,abi_binds,abi_connects,abi_listens;
static bool abi_name_peer[NAS_OPEN_FILE_LIMIT];
static bool abi_reuse_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_broadcast_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_recverr_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_pktinfo_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_nodelay_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_keepalive_enabled[NAS_OPEN_FILE_LIMIT];
static bool abi_tx_backpressure[NAS_OPEN_FILE_LIMIT];
static uint8_t abi_bound_ifindex[NAS_OPEN_FILE_LIMIT];
static bool abi_has_dhcp_ipv4=true;
static bool abi_link_carrier=true;
static bool abi_interface_up=true;
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
static bool abi_udp_sent_pktinfo;
static struct nas_lwip_bridge_pktinfo abi_udp_last_pktinfo;

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
int64_t nas_lwip_socket_sendto_pktinfo(
    unsigned slot,const uint8_t *data,uint16_t length,
    struct nas_lwip_bridge_address destination,
    const struct nas_lwip_bridge_pktinfo *pktinfo) {
    int64_t rc=nas_lwip_socket_sendto(slot,data,length,destination);
    if(rc>=0) {
        abi_udp_sent_pktinfo=pktinfo!=NULL;
        abi_udp_last_pktinfo=pktinfo ? *pktinfo :
                              (struct nas_lwip_bridge_pktinfo){0};
    }
    return rc;
}
int64_t nas_lwip_socket_recvfrom(unsigned slot,uint8_t *bytes,
                                 uint16_t capacity,
                                 struct nas_lwip_bridge_address *source,
                                 bool *truncated,
                                 struct nas_lwip_bridge_pktinfo *pktinfo) {
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
    if(pktinfo)*pktinfo=(struct nas_lwip_bridge_pktinfo){
        .ifindex=1,.spec_dst=UINT32_C(0x7f000001),
        .destination=UINT32_C(0x7f000001)
    };
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
static uint64_t abi_peek(unsigned slot,uint8_t *out,uint64_t count,
                         uint64_t offset) {
    assert(nas_lwip_socket_is_slot(slot));
    assert(abi_type[slot]==NAS_LWIP_BRIDGE_TCP);
    assert(offset<=abi_rx_length[slot]);
    if(count>abi_rx_length[slot]-offset)
        count=abi_rx_length[slot]-offset;
    if(count)memcpy(out,abi_rx_bytes[slot]+offset,(size_t)count);
    return count;
}
static uint8_t abi_tcp_sent[32];
static unsigned abi_tcp_sent_length,abi_tcp_sends;
static unsigned abi_tcp_last_write_slot;
static uint64_t abi_write_plan(unsigned slot,uint64_t count) {
    assert(nas_lwip_socket_is_slot(slot));
    assert(abi_type[slot]==NAS_LWIP_BRIDGE_TCP);
    if(abi_tx_backpressure[slot])return (uint64_t)-EOPNOTSUPP;
    return count;
}
static uint64_t abi_write(unsigned slot,const uint8_t *data,uint64_t count) {
    assert(nas_lwip_socket_is_slot(slot));
    assert(abi_type[slot]==NAS_LWIP_BRIDGE_TCP);
    assert(count<=sizeof(abi_tcp_sent));
    abi_tcp_last_write_slot=slot;
    if(count)memcpy(abi_tcp_sent,data,(size_t)count);
    abi_tcp_sent_length=(unsigned)count;
    ++abi_tcp_sends;
    return count;
}
static const struct nas_file_ops abi_ops={.release=abi_release,
    .read_plan=abi_read_plan,.read=abi_read,.peek=abi_peek,
    .write_plan=abi_write_plan,.write=abi_write,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY};
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
unsigned nas_lwip_guest_if_snapshot(
    struct nas_lwip_guest_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
    assert(out);
    out[0]=(struct nas_lwip_guest_if){
        .name="lo",.ipv4_host_order=UINT32_C(0x7f000001),.prefix_len=8,
        .mtu=65536,.ifindex=1,.up=true,.link_up=true,.loopback=true
    };
    out[1]=(struct nas_lwip_guest_if){
        .name="eth0",.ifindex=2,.mtu=1500,.up=abi_interface_up,
        .link_up=abi_link_carrier,
        .ipv4_host_order=abi_has_dhcp_ipv4 ? UINT32_C(0x0a00020f):0,
        .prefix_len=abi_has_dhcp_ipv4 ? 24:0
    };
    return 2;
}
unsigned nas_lwip_guest_qdisc_snapshot(
    struct nas_lwip_guest_qdisc out[NAS_LWIP_GUEST_IF_LIMIT]) {
    /* This isolated socket/FD host fixture never attaches the real lwIP
     * loopif's output policy. Interface existence alone is not a qdisc. */
    (void)out;
    return 0;
}
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[NAS_LWIP_GUEST_IF_LIMIT]) {
    assert(out);
    struct nas_lwip_guest_if snapshot[NAS_LWIP_GUEST_IF_LIMIT];
    unsigned count=nas_lwip_guest_if_snapshot(snapshot),available=0;
    for(unsigned i=0;i<count;i++) {
        if(!snapshot[i].ipv4_host_order || !snapshot[i].prefix_len)continue;
        out[available]=(struct nas_lwip_guest_ipv4_if){
            .ipv4_host_order=snapshot[i].ipv4_host_order,
            .prefix_len=snapshot[i].prefix_len,.mtu=snapshot[i].mtu,
            .ifindex=snapshot[i].ifindex
        };
        memcpy(out[available].name,snapshot[i].name,sizeof(out[available].name));
        available++;
    }
    return available;
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
int nas_lwip_socket_tcp_info_begin(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_TCP)return -ENOPROTOOPT;
    if(abi_control[slot])return -EALREADY;
    abi_tcp_info_pending[slot]=true;
    abi_control[slot]=true;
    abi_pending_checks[slot]=1;
    return 0;
}
int nas_lwip_socket_keepalive(unsigned slot,bool write,bool *enabled) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!enabled)return -EINVAL;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    if(write) {
        abi_keepalive_enabled[slot]=*enabled;
        abi_control[slot]=true;
        abi_pending_checks[slot]=1;
    } else *enabled=abi_keepalive_enabled[slot];
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
int nas_lwip_socket_ip_pktinfo(unsigned slot,bool write,bool *enabled) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!enabled)return -EINVAL;
    if(abi_type[slot]!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(write) {
        abi_pktinfo_enabled[slot]=*enabled;
        abi_control[slot]=true;abi_pending_checks[slot]=1;
    } else *enabled=abi_pktinfo_enabled[slot];
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
    if(abi_tcp_info_pending[slot]) {
        /* Deterministic mock *only* for ABI layout/async ownership tests.
         * The real PCB metrics are verified by test-lwip-socket-port. */
        out->tcp_metrics=(struct nas_lwip_tcp_metrics){
            .state=1,.send_mss=536,.receive_window=2144,
            .congestion_window=2144,.slow_start_threshold=4288,
            .unacked_segments=2,.retransmission_timeout_us=3000000,
            .smoothed_rtt_us=125000,.rtt_variance_us=62500,
            .current_retransmissions=1
        };
        abi_tcp_info_pending[slot]=false;
    }
    abi_control_status[slot]=0;
    return 1;
}

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
    unsigned unix_slot=guest_task.fds->entries[unix_fd_number].slot;
    assert(!nas_lwip_socket_is_slot(unix_slot));
    assert(abi_opens==0);
    assert((int64_t)sys_socket(1,2,0)==-ESOCKTNOSUPPORT);

    /* Real syscall adapter chooses official-lwIP FileOps, preserving one
     * ordinary OpenFile lifetime and the caller's SOCK_NONBLOCK/CLOEXEC. */
    /* Reproduce the real M143 failure: a last-close native Netconn still
     * owns the lower indexed slot after its File reference was dropped.
     * A new TCP socket must use a different free slot, never false ENFILE. */
    unsigned retiring_slot=0;
    while(retiring_slot<NAS_OPEN_FILE_LIMIT &&
          nas_open_files[retiring_slot].refcount)retiring_slot++;
    assert(retiring_slot<NAS_OPEN_FILE_LIMIT);
    abi_retiring_bridge[retiring_slot]=true;
    uint64_t tcp_fd=sys_socket(2,1U|0x800U|0x80000U,6);
    assert(tcp_fd<NAS_FD_LIMIT && tcp_fd!=unix_fd_number);
    unsigned tcp_slot=guest_task.fds->entries[tcp_fd].slot;
    assert(tcp_slot!=retiring_slot);
    abi_retiring_bridge[retiring_slot]=false;
    assert(abi_live[tcp_slot] && abi_type[tcp_slot]==NAS_LWIP_BRIDGE_TCP);
    assert(nas_open_files[tcp_slot].ops==&abi_ops);
    assert(nas_open_files[tcp_slot].status_flags&0x800U);
    assert(guest_task.fds->entries[tcp_fd].cloexec);
    assert(!unix_fd(tcp_fd) && lwip_fd(tcp_fd) && inet_fd(tcp_fd));
    assert(socket_fd_error(NAS_SC_recvfrom,tcp_fd)==0);
    /* Net-SNMP uses the real SIOCGIFCONF ioctl to enumerate IPv4
     * interfaces. Return Linux LP64 ifconf/ifreq bytes from the native
     * network snapshot, not ENOTTY and not a fabricated DHCP lease. */
    const uint64_t ifcaddr=TEST_BASE+1800,ifbuf=TEST_BASE+1900;
    struct {int32_t length;uint32_t pad;uint64_t buffer;} ifconf={0};
    _Static_assert(sizeof(ifconf)==16,"Linux LP64 ifconf fixture");
    ifconf.length=80;
    memcpy(user_ptr(ifcaddr),&ifconf,sizeof(ifconf));
    assert(sys_ioctl_fd(tcp_fd,0x8912ULL,ifcaddr)==0);
    memcpy(&ifconf,user_ptr(ifcaddr),sizeof(ifconf));
    assert(ifconf.length==80 && ifconf.buffer==0);
    ifconf.buffer=ifbuf;ifconf.length=40;
    memcpy(user_ptr(ifcaddr),&ifconf,sizeof(ifconf));
    assert(sys_ioctl_fd(tcp_fd,0x8912ULL,ifcaddr)==0);
    memcpy(&ifconf,user_ptr(ifcaddr),sizeof(ifconf));
    assert(ifconf.length==40 && !strcmp((char *)user_ptr(ifbuf),"lo"));
    const uint8_t lo_sockaddr[8]={2,0,0,0,127,0,0,1};
    assert(!memcmp(user_ptr(ifbuf)+16,lo_sockaddr,8));
    ifconf.length=80;
    memcpy(user_ptr(ifcaddr),&ifconf,sizeof(ifconf));
    assert(sys_ioctl_fd(tcp_fd,0x8912ULL,ifcaddr)==0);
    memcpy(&ifconf,user_ptr(ifcaddr),sizeof(ifconf));
    assert(ifconf.length==80 &&
           !strcmp((char *)user_ptr(ifbuf+40),"eth0"));
    const uint8_t eth_sockaddr[8]={2,0,0,0,10,0,2,15};
    assert(!memcmp(user_ptr(ifbuf+40)+16,eth_sockaddr,8));
    abi_has_dhcp_ipv4=false;
    assert(sys_ioctl_fd(tcp_fd,0x8912ULL,ifcaddr)==0);
    memcpy(&ifconf,user_ptr(ifcaddr),sizeof(ifconf));
    assert(ifconf.length==40);
    abi_has_dhcp_ipv4=true;
    ifconf.length=80;
    memcpy(user_ptr(ifcaddr),&ifconf,sizeof(ifconf));
    can_write[0]=false;
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8912ULL,ifcaddr)==-EFAULT);
    can_write[0]=true;
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8912ULL,0)==-EFAULT);

    /* dnsmasq resolves Linux NETLINK_ROUTE ifindices through interface
     * ioctls. An attached NIC exists before DHCP, but SIOCGIFCONF must
     * publish only an address actually assigned by lwIP. */
    const uint64_t ifraddr=TEST_BASE+2100;
    uint8_t ifreq[40]={0};
    int32_t ifindex=1;
    memcpy(ifreq+16,&ifindex,4);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert(sys_ioctl_fd(tcp_fd,0x8910ULL,ifraddr)==0);
    assert(!strcmp((const char *)user_ptr(ifraddr),"lo"));
    memcpy(ifreq,user_ptr(ifraddr),sizeof(ifreq));
    assert(sys_ioctl_fd(tcp_fd,0x8913ULL,ifraddr)==0);
    uint16_t ifflags=0;
    memcpy(&ifflags,user_ptr(ifraddr)+16,2);
    assert(ifflags==0x49U); /* IFF_UP|LOOPBACK|RUNNING */
    memset(ifreq,0,sizeof(ifreq));
    ifindex=2;memcpy(ifreq+16,&ifindex,4);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert(sys_ioctl_fd(tcp_fd,0x8910ULL,ifraddr)==0);
    assert(!strcmp((const char *)user_ptr(ifraddr),"eth0"));
    assert(sys_ioctl_fd(tcp_fd,0x8913ULL,ifraddr)==0);
    memcpy(&ifflags,user_ptr(ifraddr)+16,2);
    assert(ifflags==0x1043U); /* UP|BROADCAST|RUNNING|MULTICAST */
    assert(sys_ioctl_fd(tcp_fd,0x8921ULL,ifraddr)==0);
    uint32_t ifmtu=0;
    memcpy(&ifmtu,user_ptr(ifraddr)+16,4);
    assert(ifmtu==1500U); /* observed native Ethernet core MTU */
    /* Original libpcap/iftop queries ordinary IPv4 and ethtool ioctl
     * through AF_INET SOCK_DGRAM. The address is the actual lease, not a
     * guessed NIC constant. No hardware timestamp API is implemented:
     * Linux EOPNOTSUPP makes libpcap select its supported software path. */
    assert(sys_ioctl_fd(tcp_fd,0x8915ULL,ifraddr)==0); /* SIOCGIFADDR */
    const uint8_t actual_dhcp[8]={2,0,0,0,10,0,2,15};
    assert(!memcmp(user_ptr(ifraddr)+16,actual_dhcp,sizeof(actual_dhcp)));
    assert(sys_ioctl_fd(tcp_fd,0x891bULL,ifraddr)==0); /* NETMASK */
    const uint8_t actual_mask[8]={2,0,0,0,255,255,255,0};
    assert(!memcmp(user_ptr(ifraddr)+16,actual_mask,sizeof(actual_mask)));
    assert(sys_ioctl_fd(tcp_fd,0x8919ULL,ifraddr)==0); /* BROADCAST */
    const uint8_t actual_broadcast[8]={2,0,0,0,10,0,2,255};
    assert(!memcmp(user_ptr(ifraddr)+16,actual_broadcast,
                   sizeof(actual_broadcast)));
    const uint64_t ethtool_arg=TEST_BASE+2300;
    uint32_t ethtool_get_ts_info=0x41U;
    memcpy(user_ptr(ethtool_arg),&ethtool_get_ts_info,4);
    memset(ifreq,0,sizeof(ifreq));
    memcpy(ifreq,"eth0",5);
    memcpy(ifreq+16,&ethtool_arg,sizeof(ethtool_arg));
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8946ULL,ifraddr)==-EOPNOTSUPP);
    /* Unsupported ethtool commands are not successful no-ops. */
    uint32_t other_ethtool_command=0x7U;
    memcpy(user_ptr(ethtool_arg),&other_ethtool_command,4);
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8946ULL,ifraddr)==-EOPNOTSUPP);
    uint64_t bad_ethtool_pointer=0;
    memcpy(ifreq+16,&bad_ethtool_pointer,sizeof(bad_ethtool_pointer));
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8946ULL,ifraddr)==-EFAULT);
    bad_ethtool_pointer=0xffffffffffffffffULL;
    memcpy(ifreq+16,&bad_ethtool_pointer,sizeof(bad_ethtool_pointer));
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8946ULL,ifraddr)==-EFAULT);
    memcpy(ifreq,"missing0",9);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8946ULL,ifraddr)==-ENODEV);
    memset(ifreq,0,sizeof(ifreq));
    memcpy(ifreq,"eth0",5);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    abi_has_dhcp_ipv4=false;
    memset(ifreq,0,sizeof(ifreq));
    ifindex=2;memcpy(ifreq+16,&ifindex,4);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert(sys_ioctl_fd(tcp_fd,0x8910ULL,ifraddr)==0);
    assert(!strcmp((const char *)user_ptr(ifraddr),"eth0"));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8915ULL,ifraddr)==-EADDRNOTAVAIL);
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x891bULL,ifraddr)==-EADDRNOTAVAIL);
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8919ULL,ifraddr)==-EADDRNOTAVAIL);
    assert(sys_ioctl_fd(tcp_fd,0x8913ULL,ifraddr)==0);
    memcpy(&ifflags,user_ptr(ifraddr)+16,2);
    assert(ifflags==0x1043U); /* no DHCP lease does not remove NIC */
    abi_link_carrier=false;
    assert(sys_ioctl_fd(tcp_fd,0x8913ULL,ifraddr)==0);
    memcpy(&ifflags,user_ptr(ifraddr)+16,2);
    assert(ifflags==0x1003U); /* IFF_RUNNING drops with real carrier */
    assert(sys_ioctl_fd(tcp_fd,0x8921ULL,ifraddr)==0);
    memcpy(&ifmtu,user_ptr(ifraddr)+16,4);
    assert(ifmtu==1500U); /* MTU available during link loss */
    abi_interface_up=false;
    assert(sys_ioctl_fd(tcp_fd,0x8913ULL,ifraddr)==0);
    memcpy(&ifflags,user_ptr(ifraddr)+16,2);
    assert(ifflags==0x1002U); /* IFF_UP clears independently of IFF_RUNNING */
    abi_interface_up=true;
    abi_link_carrier=true;
    abi_has_dhcp_ipv4=true;
    ifindex=77;memset(ifreq,0,sizeof(ifreq));
    memcpy(ifreq+16,&ifindex,4);
    memcpy(user_ptr(ifraddr),ifreq,sizeof(ifreq));
    assert((int64_t)sys_ioctl_fd(tcp_fd,0x8910ULL,ifraddr)==-ENODEV);

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
    /* Original iperf3 3.19.1 invokes getsockopt(IPPROTO_TCP,TCP_INFO)
     * for each interval; this must query the genuine worker-owned TCP PCB. */
    uint64_t infoaddr=TEST_BASE+2500;
    uint32_t infolen=104;
    memcpy(user_ptr(optlen),&infolen,4);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,6,11,infoaddr,optlen)==-EAGAIN);
    assert(abi_tcp_info_pending[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_tcp_info_description(tcp_slot,infoaddr,optlen)==0);
    memcpy(&infolen,user_ptr(optlen),4);assert(infolen==104);
    assert(user_ptr(infoaddr)[0]==1); /* TCP_ESTABLISHED */
    assert(user_ptr(infoaddr)[2]==1); /* current pcb->nrtx */
    memcpy(&value,user_ptr(infoaddr)+8,4);assert(value==3000000);
    memcpy(&value,user_ptr(infoaddr)+16,4);assert(value==536);
    memcpy(&value,user_ptr(infoaddr)+20,4);assert(value==0); /* unknown rcvmss */
    memcpy(&value,user_ptr(infoaddr)+24,4);assert(value==2);
    memcpy(&value,user_ptr(infoaddr)+68,4);assert(value==125000);
    memcpy(&value,user_ptr(infoaddr)+72,4);assert(value==62500);
    memcpy(&value,user_ptr(infoaddr)+76,4);assert(value==8); /* ssthresh segments */
    memcpy(&value,user_ptr(infoaddr)+80,4);assert(value==4); /* cwnd segments */
    memcpy(&value,user_ptr(infoaddr)+100,4);assert(value==0); /* unavailable lifetime count */
    assert((int64_t)nas_fs_socket_tcp_info_description(tcp_slot,infoaddr,optlen)
           ==-EAGAIN); /* one-shot completed request */
    infolen=1;memcpy(user_ptr(optlen),&infolen,4);
    user_ptr(infoaddr)[1]=0xa5U;
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,6,11,infoaddr,optlen)==-EAGAIN);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_tcp_info_description(tcp_slot,infoaddr,optlen)==0);
    memcpy(&infolen,user_ptr(optlen),4);assert(infolen==1);
    assert(user_ptr(infoaddr)[0]==1 && user_ptr(infoaddr)[1]==0xa5U);
    infolen=512;memcpy(user_ptr(optlen),&infolen,4);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,6,11,infoaddr,optlen)==-EAGAIN);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_tcp_info_description(tcp_slot,infoaddr,optlen)==0);
    memcpy(&infolen,user_ptr(optlen),4);assert(infolen==104);
    infolen=0;memcpy(user_ptr(optlen),&infolen,4);
    assert(sys_ipv4_getsockopt(tcp_fd,6,11,0,optlen)==0);
    memcpy(&infolen,user_ptr(optlen),4);assert(infolen==0);
    infolen=104;memcpy(user_ptr(optlen),&infolen,4);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,6,11,0,optlen)==-EFAULT);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,6,11,infoaddr,0)==-EFAULT);
    assert((int64_t)sys_ipv4_getsockopt(udp_fd,6,11,infoaddr,optlen)
           ==-ENOPROTOOPT);
    memcpy(user_ptr(optlen),&capacity,4);
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
    /* Linux IP_PKTINFO must turn on real Netbuf receive metadata through
     * an async PCB/Netconn command; readback follows completed state. */
    assert(sys_ipv4_getsockopt(udp_fd,0,8,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==0);
    value=1;memcpy(user_ptr(optaddr),&value,4);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,0,8,optaddr,4)==-EOPNOTSUPP);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,0,8,optaddr,3)==-EINVAL);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,0,8,optaddr,4)==-EAGAIN);
    assert(!nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_ready(udp_slot));
    assert(nas_fs_socket_control_description(udp_slot)==0);
    assert(sys_ipv4_getsockopt(udp_fd,0,8,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==1);

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
    /* HAProxy writes HTTP replies with MSG_DONTWAIT|MSG_NOSIGNAL|MSG_MORE.
     * TCP is a byte stream: the coalescing hint may flush earlier, but
     * must never discard the payload or reject a valid sendto syscall. */
    memcpy(user_ptr(rxbuf),"HTTP",4);
    unsigned prior_sends=abi_tcp_sends;
    assert(sys_ipv4_sendto(tcp_fd,rxbuf,4,0xc040ULL,0,0)==4);
    assert(abi_tcp_sends==prior_sends+1 &&
           abi_tcp_sent_length==4 &&
           !memcmp(abi_tcp_sent,"HTTP",4));
    assert((int64_t)sys_ipv4_sendto(tcp_fd,rxbuf,4,0x800000ULL,0,0)
           ==-EOPNOTSUPP);
    uint32_t source_capacity=5;
    memcpy(user_ptr(srclen),&source_capacity,4);
    memcpy(abi_rx_bytes[tcp_slot],"PONG",4);abi_rx_length[tcp_slot]=4;
    /* Wget calls recvfrom(MSG_PEEK) before consuming HTTP status bytes.
     * Two peeks must return identical data, and a normal read must still
     * consume the original payload. Invalid writable user memory must
     * preserve the bytes even on the peek path. */
    assert(sys_ipv4_recvfrom(tcp_fd,rxbuf,4,0x2ULL,0,0)==4);
    assert(!memcmp(user_ptr(rxbuf),"PONG",4));
    assert(abi_rx_length[tcp_slot]==4);
    can_write[0]=false;
    assert((int64_t)sys_ipv4_recvfrom(
        tcp_fd,rxbuf,4,0x2ULL,0,0)==-EFAULT);
    assert(abi_rx_length[tcp_slot]==4);
    can_write[0]=true;
    struct nas_io_attempt peek_attempt={0};
    nas_fs_io_start(NAS_SC_recvfrom,tcp_fd,rxbuf,4,&peek_attempt);
    assert(!peek_attempt.pending && peek_attempt.result==4);
    assert(abi_rx_length[tcp_slot]==4);
    assert(!memcmp(user_ptr(rxbuf),"PONG",4));
    assert((int64_t)sys_ipv4_recvfrom(tcp_fd,rxbuf,4,0x42ULL,0,0)
           ==-EOPNOTSUPP);
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
    /* UDP MSG_MORE requires real datagram corking; do not silently split
     * or merge the original Alpine DNS/Ncat packet stream. */
    assert((int64_t)sys_ipv4_sendto(udp_fd,udp_payload,4,
                                   0x8000ULL,udp_target,16)
           ==-EOPNOTSUPP);
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
    const uint64_t cmsgaddr=TEST_BASE+2100;
    struct nas_lwip_bridge_pktinfo request_info={
        .ifindex=1,.spec_dst=UINT32_C(0x7f000001),
        .destination=UINT32_C(0x0a000201)
    };
    uint8_t control[32];ipv4_pktinfo_cmsg(control,request_info);
    memcpy(user_ptr(cmsgaddr),control,sizeof(control));
    msg.control=cmsgaddr;msg.controllen=32;
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert(sys_ipv4_sendmsg(udp_fd,msgaddr,0x40ULL)==4);
    assert(abi_udp_send_count==3 && abi_udp_sent_pktinfo &&
           abi_udp_last_pktinfo.ifindex==request_info.ifindex &&
           abi_udp_last_pktinfo.spec_dst==request_info.spec_dst &&
           abi_udp_last_pktinfo.destination==request_info.destination);
    uint32_t invalid=99;memcpy(user_ptr(cmsgaddr+12),&invalid,4);
    assert((int64_t)sys_ipv4_sendmsg(udp_fd,msgaddr,0x40ULL)==-EOPNOTSUPP);
    assert(abi_udp_send_count==3);
    msg.controllen=27;
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert((int64_t)sys_ipv4_sendmsg(udp_fd,msgaddr,0)==-EOPNOTSUPP);

    /* Memcached transmits an ASCII response using sendmsg() with multiple
     * iovecs on an accepted TCP socket.  The real OpenFile vector path must
     * send the data (not report EOPNOTSUPP), validate every guest vector
     * before touching the connection, and reject unsupported per-call modes. */
    const struct nas_iovec tcp_iov[2]={
        {.base=udp_payload,.length=2},
        {.base=udp_payload+2,.length=2}
    };
    memcpy(user_ptr(iovaddr),tcp_iov,sizeof(tcp_iov));
    msg=(struct nas_msghdr){.iov=iovaddr,.iovlen=2};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    unsigned old_tcp_sends=abi_tcp_sends;
    assert(sys_ipv4_sendmsg(tcp_fd,msgaddr,0x4000ULL)==4);
    assert(abi_tcp_sends==old_tcp_sends+2 &&
           abi_tcp_sent_length==2 &&
           !memcmp(abi_tcp_sent,"NG",2));
    assert((int64_t)sys_ipv4_sendmsg(tcp_fd,msgaddr,0x40ULL)==-EOPNOTSUPP);
    uint64_t prepared_iov=0,prepared_count=0;
    assert(nas_fs_socket_tcp_sendmsg_iov(tcp_fd,msgaddr,0x4000ULL,
                                        &prepared_iov,&prepared_count)==1);
    assert(prepared_iov==iovaddr && prepared_count==2);
    assert(nas_fs_socket_tcp_sendmsg_iov(tcp_fd,msgaddr,0x40ULL,
                                        &prepared_iov,&prepared_count)==-EOPNOTSUPP);
    msg.name=udp_target;
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert((int64_t)sys_ipv4_sendmsg(tcp_fd,msgaddr,0)==-EISCONN);
    msg.name=0;
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    struct nas_iovec bad_iov[2]={tcp_iov[0],{.base=UINT64_MAX,.length=2}};
    memcpy(user_ptr(iovaddr),bad_iov,sizeof(bad_iov));
    old_tcp_sends=abi_tcp_sends;
    assert((int64_t)sys_ipv4_sendmsg(tcp_fd,msgaddr,0)==-EFAULT);
    assert(abi_tcp_sends==old_tcp_sends);

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
    assert(msg.namelen==16 && msg.flags==(0x20U|0x8U) &&
           msg.controllen==0); /* data truncated, requested metadata omitted */
    assert(abi_rx_length[udp_slot]==0);
    /* Output ancillary has exact 16-byte cmsghdr, 12-byte in_pktinfo,
     * 4 bytes alignment. Insufficient control space sets MSG_CTRUNC. */
    memcpy(abi_rx_bytes[udp_slot],"INFO",4);abi_rx_length[udp_slot]=4;
    msg=(struct nas_msghdr){.name=srcaddr,.namelen=16,
        .iov=iovaddr,.iovlen=2,.control=cmsgaddr,.controllen=32};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert(sys_ipv4_recvmsg(udp_fd,msgaddr,0x40ULL)==4);
    memcpy(&msg,user_ptr(msgaddr),sizeof(msg));
    assert(msg.controllen==32 && msg.flags==0);
    uint8_t expected_info[32];ipv4_pktinfo_cmsg(expected_info,
        (struct nas_lwip_bridge_pktinfo){.ifindex=1,
            .spec_dst=UINT32_C(0x7f000001),
            .destination=UINT32_C(0x7f000001)});
    assert(!memcmp(user_ptr(cmsgaddr),expected_info,32));
    memcpy(abi_rx_bytes[udp_slot],"CUT",3);abi_rx_length[udp_slot]=3;
    msg.controllen=16;
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    assert(sys_ipv4_recvmsg(udp_fd,msgaddr,0)==3);
    memcpy(&msg,user_ptr(msgaddr),sizeof(msg));
    assert(msg.controllen==0 && msg.flags==0x8U);
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
    /* SO_KEEPALIVE must mutate a TCP PCB through the async control path,
     * expose its completed value, and reject UDP without false success. */
    enabled=0;
    memcpy(user_ptr(optaddr),&enabled,4);
    assert(sys_ipv4_getsockopt(tcp_fd,1,9,optaddr,optlen)==0);
    memcpy(&enabled,user_ptr(optaddr),4);assert(enabled==0);
    enabled=1;
    memcpy(user_ptr(optaddr),&enabled,4);
    assert((int64_t)sys_ipv4_setsockopt(udp_fd,1,9,optaddr,4)==-EOPNOTSUPP);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,1,9,optaddr,3)==-EINVAL);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,1,9,optaddr,4)==-EAGAIN);
    assert(abi_keepalive_enabled[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert(sys_ipv4_getsockopt(tcp_fd,1,9,optaddr,optlen)==0);
    memcpy(&enabled,user_ptr(optaddr),4);assert(enabled==1);
    enabled=0;
    memcpy(user_ptr(optaddr),&enabled,4);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,1,9,optaddr,4)==-EAGAIN);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert(sys_ipv4_getsockopt(tcp_fd,1,9,optaddr,optlen)==0);
    memcpy(&enabled,user_ptr(optaddr),4);assert(enabled==0);
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

    /* Dispatch routes a prepared TCP sendmsg through nas_fs_io_start's
     * held-File writev path. Force provider-level TX backpressure: blocking
     * requests park with one imported vector and retain the ORIGINAL
     * OpenFile when a sibling closes/reuses the integer descriptor. */
    uint64_t pending_fd=sys_socket(2,1,6);
    assert(pending_fd<NAS_FD_LIMIT);
    unsigned pending_slot=guest_task.fds->entries[pending_fd].slot;
    assert(!(nas_open_files[pending_slot].status_flags&0x800ULL));
    msg=(struct nas_msghdr){.iov=iovaddr,.iovlen=2};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    memcpy(user_ptr(iovaddr),tcp_iov,sizeof(tcp_iov));
    memcpy(user_ptr(udp_payload),"PING",4);
    prepared_iov=prepared_count=0;
    assert(nas_fs_socket_tcp_sendmsg_iov(pending_fd,msgaddr,0x4000ULL,
                                        &prepared_iov,&prepared_count)==1);
    struct nas_io_attempt backpressure={0};
    abi_tx_backpressure[pending_slot]=true;
    old_tcp_sends=abi_tcp_sends;
    nas_fs_io_start(NAS_SC_writev,pending_fd,prepared_iov,prepared_count,
                    &backpressure);
    assert(backpressure.pending &&
           (int64_t)backpressure.result==-EOPNOTSUPP);
    assert(nas_open_files[pending_slot].refcount==2);
    assert(abi_tcp_sends==old_tcp_sends);
    /* The iovec metadata may change or become unmapped after import; the
     * parked kernel request must never reread a new Task's user pointer. */
    memcpy(user_ptr(iovaddr),bad_iov,sizeof(bad_iov));
    nas_fs_close_fd(pending_fd);
    assert(nas_open_files[pending_slot].refcount==1 &&
           guest_task.fds->entries[pending_fd].kind==FD_CLOSED);
    uint64_t reused_fd=sys_socket(2,1,6);
    assert(reused_fd==pending_fd);
    unsigned reused_slot=guest_task.fds->entries[reused_fd].slot;
    assert(reused_slot!=pending_slot);
    abi_tx_backpressure[pending_slot]=false;
    assert(nas_fs_io_resume(backpressure.pending)==4);
    assert(abi_tcp_sends==old_tcp_sends+2 &&
           abi_tcp_last_write_slot==pending_slot &&
           !memcmp(abi_tcp_sent,"NG",2));
    nas_fs_io_dispose(backpressure.pending);
    assert(!nas_open_files[pending_slot].refcount &&
           !abi_live[pending_slot] && abi_live[reused_slot]);
    nas_fs_close_fd(reused_fd);
    /* Full import atomicity: invalid second iovec must never send the
     * first iovec, and too many vectors must fail before provider I/O. */
    uint64_t invalid_fd=sys_socket(2,1,6);
    assert(invalid_fd<NAS_FD_LIMIT);
    msg=(struct nas_msghdr){.iov=iovaddr,.iovlen=2};
    memcpy(user_ptr(msgaddr),&msg,sizeof(msg));
    memcpy(user_ptr(iovaddr),bad_iov,sizeof(bad_iov));
    old_tcp_sends=abi_tcp_sends;
    nas_fs_io_start(NAS_SC_writev,invalid_fd,iovaddr,2,&backpressure);
    assert(!backpressure.pending && (int64_t)backpressure.result==-EFAULT);
    assert(abi_tcp_sends==old_tcp_sends);
    nas_fs_io_start(NAS_SC_writev,invalid_fd,iovaddr,1025,&backpressure);
    assert(!backpressure.pending && (int64_t)backpressure.result==-EINVAL);
    assert(abi_tcp_sends==old_tcp_sends);
    nas_fs_close_fd(invalid_fd);
    /* Exercise the ACTUAL shared syscall dispatcher against the real
     * kernel NETLINK_ROUTE provider, including guest msghdr usercopy.
     * The standalone backend test covers all multipart/header bytes. */
    uint64_t nl_fd=sys_socket(16,3,0);
    assert(nl_fd<NAS_FD_LIMIT && netlink_fd(nl_fd) &&
           !unix_fd(nl_fd) && !inet_fd(nl_fd));
    assert(nas_open_files[guest_task.fds->entries[nl_fd].slot].ops==
           nas_netlink_route_ops());
    const uint64_t nl_name=TEST_BASE+TEST_PAGE+300;
    const uint64_t nl_packet=TEST_BASE+TEST_PAGE+350;
    const uint64_t nl_message=TEST_BASE+TEST_PAGE+450;
    const uint64_t nl_iovec=TEST_BASE+TEST_PAGE+520;
    const uint64_t nl_reply=TEST_BASE+TEST_PAGE+600;
    const uint64_t nl_source=TEST_BASE+TEST_PAGE+700;
    const uint64_t nl_length=TEST_BASE+TEST_PAGE+800;
    const uint8_t nl_kernel[12]={16,0,0,0,0,0,0,0,0,0,0,0};
    memcpy(user_ptr(nl_name),nl_kernel,sizeof(nl_kernel));
    assert(sys_netlink_bind(nl_fd,nl_name,12)==0);
    uint32_t nl_cap=12;
    memcpy(user_ptr(nl_length),&nl_cap,4);
    assert(sys_netlink_name(nl_fd,nl_name,nl_length)==0);
    assert(user_ptr(nl_name)[0]==16 && user_ptr(nl_name)[1]==0);
    /* getsockname returned the bound USER port; sendto targets KERNEL 0. */
    memcpy(user_ptr(nl_name),nl_kernel,sizeof(nl_kernel));
    const uint8_t nl_getaddr[20]={20,0,0,0,22,0,5,3,123,0,0,0,
                                   0,0,0,0,2,0,0,0};
    memcpy(user_ptr(nl_packet),nl_getaddr,sizeof(nl_getaddr));
    assert(sys_netlink_sendto(nl_fd,nl_packet,20,0,nl_name,12)==20);
    struct nas_iovec nl_iov={.base=nl_reply,.length=8};
    memcpy(user_ptr(nl_iovec),&nl_iov,sizeof(nl_iov));
    struct nas_msghdr nl_msg={.name=nl_source,.namelen=12,
                             .iov=nl_iovec,.iovlen=1};
    memcpy(user_ptr(nl_message),&nl_msg,sizeof(nl_msg));
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0x22)==40);
    memcpy(&nl_msg,user_ptr(nl_message),sizeof(nl_msg));
    assert((nl_msg.flags&0x20U) && nl_msg.namelen==12);
    nl_iov.length=64;memcpy(user_ptr(nl_iovec),&nl_iov,sizeof(nl_iov));
    /* consuming read of the very same 40-byte record */
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==40);
    assert(user_ptr(nl_reply)[4]==20 && user_ptr(nl_reply)[16]==2 &&
           user_ptr(nl_reply)[17]==8 && user_ptr(nl_reply)[28]==127);
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==40); /* eth0 */
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==20); /* NLMSG_DONE */
    assert(user_ptr(nl_reply)[4]==3 && user_ptr(nl_reply)[16]==0 &&
           user_ptr(nl_reply)[17]==0 && user_ptr(nl_reply)[18]==0 &&
           user_ptr(nl_reply)[19]==0);
    /* Original iproute2 uses send(fd,...) with a NULL destination. Prove
     * the shared Linux syscall adapter passes that legitimate default
     * kernel endpoint through the real NETLINK_ROUTE File provider. */
    assert(sys_netlink_sendto(nl_fd,nl_packet,20,0,0,0)==20);
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==40);
    assert(user_ptr(nl_reply)[4]==20 && user_ptr(nl_reply)[16]==2);
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==40);
    assert(sys_netlink_recvmsg(nl_fd,nl_message,0)==20);

    /* Real musl getifaddrs calls recv(), which musl implements as
     * recvfrom(fd,buffer,len,flags,NULL,NULL). Exercise the real native
     * syscall adapter against the same NETLINK_ROUTE provider queue;
     * previous tests only exercised recvmsg and missed this Linux ABI. */
    uint64_t nl_recv_args[7]={0,nl_fd,nl_reply,64,0,0,0};
    int64_t nl_result=(int64_t)sys_netlink_recvfrom(nl_recv_args[1],
        nl_recv_args[2],nl_recv_args[3],nl_recv_args[4],
        nl_recv_args[5],nl_recv_args[6]);
    assert(nl_result==-EAGAIN);
    assert(sys_netlink_sendto(nl_fd,nl_packet,20,0,0,0)==20);
    /* Rejected flags and usercopy failures must leave the first datagram
     * untouched, not silently consume it and corrupt the multipart stream. */
    nl_recv_args[4]=1; /* MSG_OOB is not supported for Netlink datagrams */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,64,1,0,0);
    assert(nl_result==-EOPNOTSUPP);
    nl_recv_args[4]=0x40; /* musl MSG_DONTWAIT */
    nl_recv_args[2]=TEST_BASE+TEST_PAGES*TEST_PAGE; /* unmapped */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_recv_args[2],64,0x40,0,0);
    assert(nl_result==-EFAULT);
    nl_recv_args[2]=nl_reply;
    nl_recv_args[5]=nl_source;
    nl_recv_args[6]=0; /* populated sockaddr requires a socklen_t pointer */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,64,0x40,nl_source,0);
    assert(nl_result==-EFAULT);
    nl_recv_args[6]=TEST_BASE+TEST_PAGES*TEST_PAGE;
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,64,0x40,
                     nl_source,nl_recv_args[6]);
    assert(nl_result==-EFAULT);
    nl_recv_args[6]=nl_length;
    nl_cap=12;memcpy(user_ptr(nl_length),&nl_cap,4);
    nl_recv_args[3]=8;
    nl_recv_args[4]=0x2|0x20|0x40; /* PEEK | TRUNC | DONTWAIT */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,8,0x2|0x20|0x40,
                     nl_source,nl_length);
    assert(nl_result==40);
    assert(user_ptr(nl_reply)[4]==20 && user_ptr(nl_source)[0]==16 &&
           user_ptr(nl_source)[1]==0);
    memcpy(&nl_cap,user_ptr(nl_length),4);
    assert(nl_cap==12);
    /* PEEK preserved the first packet; this ordinary recvfrom consumes it
     * and returns only bytes copied, NOT the full datagram's 40 bytes. */
    nl_recv_args[3]=40;nl_recv_args[4]=0x40;
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,40,0x40,
                     nl_source,nl_length);
    assert(nl_result==40);
    assert(user_ptr(nl_reply)[4]==20 && user_ptr(nl_reply)[16]==2 &&
           user_ptr(nl_reply)[28]==127);
    /* Source socklen truncation reports the ACTUAL 12-byte sockaddr_nl,
     * while preserving the 4-byte caller-provided output prefix. */
    nl_cap=4;memcpy(user_ptr(nl_length),&nl_cap,4);
    nl_recv_args[3]=8;nl_recv_args[4]=0x20; /* MSG_TRUNC without PEEK */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,8,0x20,
                     nl_source,nl_length);
    assert(nl_result==40);
    memcpy(&nl_cap,user_ptr(nl_length),4);
    assert(nl_cap==12 && user_ptr(nl_source)[0]==16);
    nl_recv_args[3]=64;nl_recv_args[4]=0;nl_recv_args[5]=0;
    nl_recv_args[6]=0; /* ordinary musl recv() has no source address */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,64,0,0,0);
    assert(nl_result==20);
    assert(user_ptr(nl_reply)[4]==3); /* NLMSG_DONE */
    nl_result=(int64_t)sys_netlink_recvfrom(nl_fd,nl_reply,64,0,0,0);
    assert(nl_result==-EAGAIN);
    /* M215: original Alpine's rtmon must block on an empty ROUTE socket,
     * rather than observe spurious EAGAIN. Import while the Task owns its
     * address space; the pinned OpenFile survives close/reused fd numbers. */
    struct nas_netlink_receive_attempt nl_wait;
    uint64_t nl_wait_args[7]={0,nl_fd,nl_reply,64,0,0,0};
    unsigned held_nl_slot=guest_task.fds->entries[nl_fd].slot;
    unsigned nl_refs=nas_open_files[held_nl_slot].refcount;
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nl_wait_args,&nl_wait);
    assert(nl_wait.applicable && nl_wait.pending &&
           nl_wait.slot==held_nl_slot &&
           nas_open_files[held_nl_slot].refcount==nl_refs+1U);
    assert((int64_t)nas_fs_netlink_receive_resume(nl_wait.pending)==-EAGAIN);
    assert(!(nas_fs_poll_description(held_nl_slot,1)&1));
    nas_fs_netlink_receive_release(nl_wait.pending);
    nas_fs_drop_kernel_description(held_nl_slot);
    assert(nas_open_files[held_nl_slot].refcount==nl_refs);
    nl_wait_args[4]=0x40; /* per-call MSG_DONTWAIT never parks */
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nl_wait_args,&nl_wait);
    assert(nl_wait.applicable && !nl_wait.pending &&
           (int64_t)nl_wait.result==-EAGAIN);
    nl_wait_args[4]=0;
    nas_open_files[held_nl_slot].status_flags|=0x800ULL; /* O_NONBLOCK */
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nl_wait_args,&nl_wait);
    assert(!nl_wait.pending && (int64_t)nl_wait.result==-EAGAIN);
    nas_open_files[held_nl_slot].status_flags&=~0x800ULL;
    nl_wait_args[4]=1; /* unsupported flags preflight before parking */
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nl_wait_args,&nl_wait);
    assert(!nl_wait.pending && (int64_t)nl_wait.result==-EOPNOTSUPP);
    nl_wait_args[4]=0;
    nl_wait_args[2]=TEST_BASE+TEST_PAGES*TEST_PAGE;
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nl_wait_args,&nl_wait);
    assert(!nl_wait.pending && (int64_t)nl_wait.result==-EFAULT);
    nl_wait_args[2]=nl_reply;

    /* Unlike a second call to sys_netlink_recvmsg(fd,...), the pending
     * request's ORIGINAL user iovec, slot and name are immutable. */
    nl_iov=(struct nas_iovec){.base=nl_reply,.length=64};
    nl_msg=(struct nas_msghdr){.name=nl_source,.namelen=12,
                             .iov=nl_iovec,.iovlen=1};
    memcpy(user_ptr(nl_iovec),&nl_iov,sizeof(nl_iov));
    memcpy(user_ptr(nl_message),&nl_msg,sizeof(nl_msg));
    nl_wait_args[2]=nl_message;
    nl_wait_args[3]=0;
    nas_fs_netlink_receive_start(NAS_SC_recvmsg,nl_wait_args,&nl_wait);
    assert(nl_wait.pending && nl_wait.slot==held_nl_slot);
    nl_iov.base=TEST_BASE+TEST_PAGES*TEST_PAGE;
    nl_msg.iov=TEST_BASE+TEST_PAGES*TEST_PAGE;
    memcpy(user_ptr(nl_iovec),&nl_iov,sizeof(nl_iov));
    memcpy(user_ptr(nl_message),&nl_msg,sizeof(nl_msg));
    nas_fs_close_fd(nl_fd);
    assert(nas_open_files[held_nl_slot].refcount==1);
    uint64_t nl_reused=sys_socket(16,3,NAS_NETLINK_GENERIC_PROTOCOL);
    assert(nl_reused==nl_fd &&
           guest_task.fds->entries[nl_reused].slot!=held_nl_slot);
    assert(nas_netlink_core_sendto(held_nl_slot,nl_getaddr,20,NULL,0)==20);
    assert(nas_fs_poll_description(held_nl_slot,1)&1);
    assert(nas_fs_netlink_receive_resume(nl_wait.pending)==40);
    assert(user_ptr(nl_reply)[4]==20 && user_ptr(nl_reply)[16]==2);
    nas_fs_netlink_receive_release(nl_wait.pending);
    nas_fs_drop_kernel_description(held_nl_slot);
    assert(!nas_netlink_core_is_slot(held_nl_slot));
    nas_fs_close_fd(nl_reused);
    nl_iov=(struct nas_iovec){.base=nl_reply,.length=64};
    nl_msg=(struct nas_msghdr){.name=nl_source,.namelen=12,
                             .iov=nl_iovec,.iovlen=1};
    memcpy(user_ptr(nl_iovec),&nl_iov,sizeof(nl_iov));
    memcpy(user_ptr(nl_message),&nl_msg,sizeof(nl_msg));
    /* Keep ROUTE and NETFILTER as two actual kernel-owned OpenFile
     * providers behind the SAME syscall/usercopy/FD adapter. An original
     * nft binary queries the real nf_tables generation before transactions. */
    uint64_t nf_fd=sys_socket(16,3,NAS_NETLINK_FILTER_PROTOCOL);
    assert(nf_fd<NAS_FD_LIMIT && netlink_fd(nf_fd) &&
           !unix_fd(nf_fd) && !inet_fd(nf_fd));
    unsigned nf_slot=guest_task.fds->entries[nf_fd].slot;
    assert(nas_open_files[nf_slot].ops==nas_netlink_filter_ops());
    uint64_t nf_wait_args[7]={0,nf_fd,nl_reply,64,0,0,0};
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,nf_wait_args,&nl_wait);
    assert(nl_wait.applicable && nl_wait.pending && nl_wait.slot==nf_slot);
    assert((int64_t)nas_fs_netlink_receive_resume(nl_wait.pending)==-EAGAIN);
    nas_fs_netlink_receive_release(nl_wait.pending);
    nas_fs_drop_kernel_description(nf_slot);
    nl_cap=4;
    memcpy(user_ptr(nl_length),&nl_cap,4);
    assert(sys_netlink_getsockopt(nf_fd,1,38,nl_reply,nl_length)==0);
    uint32_t nf_protocol=0;
    memcpy(&nf_protocol,user_ptr(nl_reply),4);
    assert(nf_protocol==NAS_NETLINK_FILTER_PROTOCOL);
    const uint8_t get_generation[20]={20,0,0,0,0x10,0x0a,1,0,
                                      0x34,0x12,0,0,0,0,0,0,0,0,0,0};
    memcpy(user_ptr(nl_packet),get_generation,sizeof(get_generation));
    assert(sys_netlink_sendto(nf_fd,nl_packet,sizeof(get_generation),0,0,0)==
           sizeof(get_generation));
    /* The new recvfrom ABI is shared by NETLINK_NETFILTER too: PEEK sees
     * the genuine generation reply, then recvmsg consumes that same record. */
    assert((int64_t)sys_netlink_recvfrom(nf_fd,nl_reply,32,0x42,0,0)==28);
    assert(user_ptr(nl_reply)[4]==15 && user_ptr(nl_reply)[5]==10);
    assert(sys_netlink_recvmsg(nf_fd,nl_message,0)==28);
    assert(user_ptr(nl_reply)[4]==15 && user_ptr(nl_reply)[5]==10 &&
           user_ptr(nl_reply)[8]==0x34 && user_ptr(nl_reply)[9]==0x12);
    nas_fs_close_fd(nf_fd);

    /* SOCK_DIAG is another genuine protocol namespace of the ROUTE owner.
     * Every Netlink protocol must use identical fd/usercopy/socketopt ABI. */
    uint64_t diag_fd=sys_socket(16,3,4);
    assert(diag_fd<NAS_FD_LIMIT && netlink_fd(diag_fd));
    assert(nas_open_files[guest_task.fds->entries[diag_fd].slot].ops==
           nas_netlink_route_ops());
    nl_cap=4;memcpy(user_ptr(nl_length),&nl_cap,4);
    assert(sys_netlink_getsockopt(diag_fd,1,38,nl_reply,nl_length)==0);
    memcpy(&nf_protocol,user_ptr(nl_reply),4);
    assert(nf_protocol==4);
    memcpy(user_ptr(nl_name),nl_kernel,12);
    assert(sys_netlink_bind(diag_fd,nl_name,12)==0);
    nas_fs_close_fd(diag_fd);

    /* Original Alpine's `/sbin/genl ctrl list` uses NETLINK_GENERIC (16)
     * and real GETFAMILY. Discover exactly the nlctrl provider, without
     * inventing nl80211, devlink or any unimplemented family. */
    uint64_t genl_fd=sys_socket(16,3,NAS_NETLINK_GENERIC_PROTOCOL);
    assert(genl_fd<NAS_FD_LIMIT && netlink_fd(genl_fd) &&
           !unix_fd(genl_fd) && !inet_fd(genl_fd));
    unsigned genl_slot=guest_task.fds->entries[genl_fd].slot;
    assert(nas_open_files[genl_slot].ops==nas_netlink_generic_ops());
    uint64_t genl_wait_args[7]={0,genl_fd,nl_reply,64,0,0,0};
    nas_fs_netlink_receive_start(NAS_SC_recvfrom,genl_wait_args,&nl_wait);
    assert(nl_wait.applicable && nl_wait.pending && nl_wait.slot==genl_slot);
    assert((int64_t)nas_fs_netlink_receive_resume(nl_wait.pending)==-EAGAIN);
    nas_fs_netlink_receive_release(nl_wait.pending);
    nas_fs_drop_kernel_description(genl_slot);
    nl_cap=4;memcpy(user_ptr(nl_length),&nl_cap,4);
    assert(sys_netlink_getsockopt(genl_fd,1,38,nl_reply,nl_length)==0);
    memcpy(&nf_protocol,user_ptr(nl_reply),4);
    assert(nf_protocol==NAS_NETLINK_GENERIC_PROTOCOL);
    uint32_t quota=4096;
    memcpy(user_ptr(nl_reply),&quota,4);
    assert(sys_netlink_setsockopt(genl_fd,1,7,nl_reply,4)==0);
    assert(sys_netlink_setsockopt(genl_fd,1,8,nl_reply,4)==0);
    assert(sys_netlink_getsockopt(genl_fd,1,7,nl_reply,nl_length)==0);
    memcpy(&quota,user_ptr(nl_reply),4);
    assert(quota==8192);

    /* GETFAMILY by name: 16-byte nlmsghdr + 4-byte genlmsghdr +
     * CTRL_ATTR_FAMILY_NAME NLA, padded to the next 4-byte boundary. */
    const uint8_t genl_name_req[32]={
        32,0,0,0,16,0,1,0,0x45,0x23,0,0,0,0,0,0,
        3,2,0,0,11,0,2,0,'n','l','c','t','r','l',0,0
    };
    memcpy(user_ptr(nl_packet),genl_name_req,sizeof(genl_name_req));
    assert(sys_netlink_sendto(genl_fd,nl_packet,32,0,0,0)==32);
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,8,0x22,0,0)>32);
    assert(user_ptr(nl_reply)[4]==16);
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,96,0,0,0)>32);
    assert(user_ptr(nl_reply)[4]==16 && user_ptr(nl_reply)[16]==1 &&
           user_ptr(nl_reply)[8]==0x45 && user_ptr(nl_reply)[9]==0x23);
    /* The first published family NLA must truthfully name nlctrl. */
    assert(user_ptr(nl_reply)[22]==2 &&
           !memcmp(user_ptr(nl_reply)+24,"nlctrl",7));
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,96,0,0,0)==-EAGAIN);

    /* Netlink controller dump: one advertised registered family, followed
     * by Linux's multipart NLMSG_DONE. An unknown family produces ENOENT
     * as NLMSG_ERROR, not a fabricated empty or successful family. */
    const uint8_t genl_dump[20]={20,0,0,0,16,0,1,3,99,0,0,0,
                                  0,0,0,0,3,2,0,0};
    memcpy(user_ptr(nl_packet),genl_dump,sizeof(genl_dump));
    assert(sys_netlink_sendto(genl_fd,nl_packet,20,0,nl_name,12)==20);
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,96,0,0,0)>32);
    assert(user_ptr(nl_reply)[4]==16 && (user_ptr(nl_reply)[6]&2));
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,96,0,0,0)==20);
    assert(user_ptr(nl_reply)[4]==3 && user_ptr(nl_reply)[16]==0);
    uint8_t genl_missing[32];
    memcpy(genl_missing,genl_name_req,sizeof(genl_missing));
    memcpy(genl_missing+24,"absent",7);
    memcpy(user_ptr(nl_packet),genl_missing,sizeof(genl_missing));
    assert(sys_netlink_sendto(genl_fd,nl_packet,32,0,0,0)==32);
    assert((int64_t)sys_netlink_recvfrom(genl_fd,nl_reply,96,0,0,0)==36);
    assert(user_ptr(nl_reply)[4]==2);
    int32_t genl_error=0;
    memcpy(&genl_error,user_ptr(nl_reply)+16,4);
    assert(genl_error==-ENOENT);

    /* A distinct NETLINK_GENERIC socket must not hijack an in-use bound
     * pid. File closing drops only its own port/reply-queue state. */
    memcpy(user_ptr(nl_name),nl_kernel,12);
    assert((int64_t)sys_netlink_bind(genl_fd,nl_name,12)==-EINVAL); /* auto-bound */
    nl_cap=12;memcpy(user_ptr(nl_length),&nl_cap,4);
    assert(sys_netlink_name(genl_fd,nl_name,nl_length)==0);
    uint64_t genl_other=sys_socket(16,3,NAS_NETLINK_GENERIC_PROTOCOL);
    assert(genl_other<NAS_FD_LIMIT && netlink_fd(genl_other));
    assert((int64_t)sys_netlink_bind(genl_other,nl_name,12)==-EADDRINUSE);
    nas_fs_close_fd(genl_other);
    nas_fs_close_fd(genl_fd);
    assert((int64_t)sys_socket(16,3,UINT64_C(0x100000010))==
           -EPROTONOSUPPORT);
    assert((int64_t)sys_socket(16,3,13)==-EPROTONOSUPPORT);
    puts("[LWIP SENDMSG BLOCKING PASS] full iovec import, TX park/resume,");
    puts("  immutable vector snapshot, held OpenFile across close/fd reuse");
    puts("[LWIP SOCKET ABI PASS] native AF_INET TCP/UDP OpenFile + user sockaddr,");
    puts("  async control/GETNAME/SHUTDOWN/REUSEADDR, AF_UNIX preserved");
    puts("[NETLINK FILTER ABI PASS] real nf_tables GETGEN via shared LP64 socket adapter");
    puts("[NETLINK CORE ABI PASS] ROUTE/DIAG/FILTER/GENERIC shared syscall, FD, PID, options");
    puts("[NETLINK GENERIC ABI PASS] nlctrl lookup/dump, ENOENT, PEEK/TRUNC, close");
    puts("[NETLINK BLOCKING ABI PASS] ROUTE/FILTER/GENERIC immutable recvmsg, O_NONBLOCK/DONTWAIT, pinned OpenFile FD reuse");
    return 0;
}
