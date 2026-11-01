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
static unsigned abi_shutdown_how[NAS_OPEN_FILE_LIMIT];
static unsigned abi_getnames,abi_reuses,abi_shutdowns;

static void abi_release(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && abi_live[slot]);
    abi_live[slot]=false;abi_releases++;
}
static const struct nas_file_ops abi_ops={.release=abi_release};
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
bool nas_lwip_socket_is_listening(unsigned slot) {
    return nas_lwip_socket_is_slot(slot) && abi_listening[slot];
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
int nas_lwip_socket_control_ready(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot) || !abi_control[slot])return 0;
    if(abi_pending_checks[slot]) {abi_pending_checks[slot]--;return 0;}
    return 1;
}
int nas_lwip_socket_control_finish(unsigned slot) {
    if(!nas_lwip_socket_is_slot(slot))return -EBADF;
    if(!abi_control[slot])return -EINPROGRESS;
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
    assert(sys_ipv4_getsockopt(tcp_fd,1,30,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==1);
    abi_so_error[tcp_slot]=-ECONNREFUSED;
    assert(sys_ipv4_getsockopt(tcp_fd,1,4,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==ECONNREFUSED);
    assert(sys_ipv4_getsockopt(tcp_fd,1,4,optaddr,optlen)==0);
    memcpy(&value,user_ptr(optaddr),4);assert(value==0);
    assert((int64_t)sys_ipv4_getsockopt(tcp_fd,1,999,optaddr,optlen)==-ENOPROTOOPT);

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

    /* SO_REUSEADDR and SHUTDOWN both wait for a native control result.
     * Unsupported or invalid controls must not pretend to have succeeded. */
    uint32_t enabled=1;
    memcpy(user_ptr(optaddr),&enabled,4);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,1,2,optaddr,4)==-EAGAIN);
    assert(abi_reuses==1 && abi_reuse_enabled[tcp_slot]);
    assert(!nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_ready(tcp_slot));
    assert(nas_fs_socket_control_description(tcp_slot)==0);
    assert((int64_t)sys_ipv4_setsockopt(tcp_fd,6,1,optaddr,4)==-ENOPROTOOPT);
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
