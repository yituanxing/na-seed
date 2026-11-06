/* Real NaSeed Linux accept/accept4 syscall + global fd.c lease fixture.
 * Only the upstream native Netconn worker is stubbed: the separately hosted
 * lwIP tests execute its actual TCP handshake and ACCEPT lease cancellation.
 * The exact syscall/usercopy/FD publication and Task interruption remain real. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/fs/socket_syscall.c"
#include "../kernel/task/wait.h"

#define ACCEPT_USER_BASE UINT64_C(0x20000)
#define ACCEPT_USER_BYTES 128U
#define LISTENER_SLOT 7U
static uint8_t user_bytes[ACCEPT_USER_BYTES];
static bool user_writable=true;
static struct nas_task test_task;
static struct nas_files *test_files;
#define test_fds (test_files->table)
struct nas_task *nas_current_task=&test_task;

static bool mapped(uint64_t ptr,uint64_t size) {
    return ptr>=ACCEPT_USER_BASE && size<=ACCEPT_USER_BYTES &&
           ptr-ACCEPT_USER_BASE<=ACCEPT_USER_BYTES-size;
}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t count) {
    if(!mapped(addr,count))return false;
    memcpy(dst,user_bytes+(size_t)(addr-ACCEPT_USER_BASE),(size_t)count);
    return true;
}
bool nas_user_prepare_write(uint64_t addr,uint64_t count) {
    return user_writable && mapped(addr,count);
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t count) {
    if(!nas_user_prepare_write(addr,count))return false;
    memcpy(user_bytes+(size_t)(addr-ACCEPT_USER_BASE),src,(size_t)count);
    return true;
}
void nas_print(const char *msg){(void)msg;}
void nas_dec(uint64_t count){(void)count;}
void nas_arch_halt(void){abort();}
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind){(void)kind;return NULL;}

static unsigned bridge_begin_count,bridge_complete_count,bridge_cancel_count;
static unsigned bridge_release_count,adopt_count,release_count;
static bool have_peer,bridge_pending,bridge_complete,bridge_native_empty;
static bool prefetch_owned;
static struct nas_fd_slot_lease prefetch_lease;
static bool name_pending,name_ready,control_pending;
static bool name_requested_peer,reuse_enabled;
static unsigned shutdown_how,shutdown_count,reuse_count;
static struct nas_fd_slot_lease active_lease;
static const struct nas_lwip_bridge_address actual_peer={
    .ipv4=UINT32_C(0x7f000001),.port=35017
};
static void accepted_release(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT);
    release_count++;
}
static const struct nas_file_ops accepted_ops={.release=accepted_release};
const struct nas_file_ops *nas_lwip_socket_port_ops(void){return &accepted_ops;}
bool nas_lwip_socket_is_slot(unsigned slot){return slot==LISTENER_SLOT;}
/* This focused accept fixture contains only AF_INET native TCP; genuine
 * NETLINK_ROUTE lifecycle is covered by its separate host/backend tests. */
bool nas_netlink_route_is_slot(unsigned slot){(void)slot;return false;}
bool nas_packet_socket_is_slot(unsigned slot){(void)slot;return false;}
int nas_lwip_socket_type(unsigned slot){
    return slot==LISTENER_SLOT?NAS_LWIP_BRIDGE_TCP:-EBADF;
}
int nas_lwip_socket_accept_begin(unsigned slot,struct nas_fd_slot_lease lease) {
    assert(slot==LISTENER_SLOT && nas_fd_slot_lease_valid(lease));
    ++bridge_begin_count;
    if(!have_peer)return -EAGAIN;
    assert(!bridge_pending);
    bridge_pending=true;active_lease=lease;
    return 0;
}
int nas_lwip_socket_accept_complete(unsigned slot,
                                    struct nas_fd_slot_lease lease,
                                    struct nas_lwip_bridge_id *accepted,
                                    struct nas_lwip_bridge_address *peer) {
    assert(slot==LISTENER_SLOT && bridge_pending);
    assert(lease.slot==active_lease.slot && lease.token==active_lease.token);
    ++bridge_complete_count;
    if(!bridge_complete)return 0;
    bridge_pending=false;bridge_complete=false;
    if(bridge_native_empty)return -EAGAIN;
    *accepted=(struct nas_lwip_bridge_id){
        .slot=(uint16_t)lease.slot,.generation=++adopt_count
    };
    if(peer)*peer=actual_peer;
    return 1;
}
int nas_lwip_socket_accept_cancel(unsigned slot,
                                  struct nas_fd_slot_lease lease) {
    assert(slot==LISTENER_SLOT && bridge_pending);
    assert(lease.slot==active_lease.slot && lease.token==active_lease.token);
    bridge_pending=false;
    ++bridge_cancel_count;
    return 0;
}
int nas_lwip_socket_accept_prefetch_start(unsigned slot) {
    assert(slot==LISTENER_SLOT);
    if(prefetch_owned)return -EALREADY;
    if(!have_peer)return -EAGAIN;
    int rc=nas_fd_slot_lease_begin(&prefetch_lease,"mock prefetch");
    if(rc)return rc;
    rc=nas_lwip_socket_accept_begin(slot,prefetch_lease);
    if(rc) {
        (void)nas_fd_slot_lease_cancel(prefetch_lease);
        return rc;
    }
    prefetch_owned=true;
    return 0;
}
int nas_lwip_socket_accept_prefetched(unsigned slot,
                                      struct nas_fd_slot_lease *lease,
                                      struct nas_lwip_bridge_id *accepted,
                                      struct nas_lwip_bridge_address *peer) {
    assert(slot==LISTENER_SLOT && lease && accepted && peer);
    if(!prefetch_owned)return 0;
    int rc=nas_lwip_socket_accept_complete(slot,prefetch_lease,accepted,peer);
    if(rc==0)return -EAGAIN;
    prefetch_owned=false;
    if(rc==1)*lease=prefetch_lease;
    else (void)nas_fd_slot_lease_cancel(prefetch_lease);
    prefetch_lease=(struct nas_fd_slot_lease){0};
    return rc;
}
int nas_lwip_socket_port_adopt(struct nas_lwip_bridge_id accepted,
                                bool inherited_keepalive) {
    assert(!inherited_keepalive);
    assert(accepted.slot==active_lease.slot && accepted.generation);
    assert(nas_fd_slot_lease_valid(active_lease));
    return 0;
}
int nas_lwip_socket_port_set_peer(unsigned slot,
                                  struct nas_lwip_bridge_address peer) {
    assert(slot==active_lease.slot && nas_fd_slot_lease_valid(active_lease));
    assert(peer.ipv4==actual_peer.ipv4 && peer.port==actual_peer.port);
    return 0;
}
int nas_lwip_socket_peer_snapshot(unsigned slot,
                                  struct nas_lwip_bridge_address *peer) {
    assert(slot==LISTENER_SLOT && peer);
    *peer=actual_peer;
    return 0;
}
int nas_lwip_bridge_release(struct nas_lwip_bridge_id id) {
    assert(id.generation);
    ++bridge_release_count;
    return 0;
}
int nas_lwip_socket_getname_begin(unsigned slot,bool peer) {
    assert(slot==LISTENER_SLOT && !name_pending);
    name_pending=true;name_requested_peer=peer;
    return 0;
}
int nas_lwip_socket_getname_complete(unsigned slot,
                                     struct nas_lwip_bridge_address *out) {
    assert(slot==LISTENER_SLOT && name_pending && out);
    if(!name_ready)return 0;
    *out=actual_peer;
    name_pending=false;name_ready=false;
    return 1;
}
int nas_lwip_socket_shutdown(unsigned slot,unsigned how) {
    assert(slot==LISTENER_SLOT && how<=2 && !control_pending);
    control_pending=true;shutdown_how=how;shutdown_count++;
    return 0;
}
int nas_lwip_socket_setreuseaddr(unsigned slot,bool enabled) {
    assert(slot==LISTENER_SLOT && !control_pending);
    control_pending=true;reuse_enabled=enabled;reuse_count++;
    return 0;
}
int nas_lwip_socket_setbroadcast(unsigned slot,bool enabled) {
    (void)slot;
    (void)enabled;
    return -EOPNOTSUPP;
}
/* This fixture exercises ACCEPT lease handoff, not the independently tested
 * real SO_BINDTODEVICE/PCB operation. Keep its uninvoked symbols explicit. */
int nas_lwip_guest_ifindex(const char *name) {
    (void)name;assert(!"unexpected NIC selection in ACCEPT fixture");return -ENODEV;
}
int nas_lwip_guest_ifname(unsigned index,
                         char output[NAS_LWIP_DEVICE_NAME_MAX]) {
    (void)index;(void)output;
    assert(!"unexpected NIC name query in ACCEPT fixture");return -ENODEV;
}
int nas_lwip_socket_bind_if(unsigned slot,unsigned index) {
    (void)slot;(void)index;
    assert(!"unexpected interface bind in ACCEPT fixture");return -ENOSYS;
}
int nas_lwip_socket_bound_ifindex(unsigned slot) {
    (void)slot;
    assert(!"unexpected interface binding query in ACCEPT fixture");return -ENOSYS;
}
int nas_lwip_socket_tcp_nodelay(unsigned slot,bool write,bool *enabled) {
    (void)slot;
    (void)write;
    (void)enabled;
    return -EOPNOTSUPP;
}
int nas_lwip_socket_keepalive(unsigned slot,bool write,bool *enabled) {
    assert(slot==LISTENER_SLOT && !write && enabled);
    *enabled=false;
    return 0;
}
int nas_lwip_socket_buffer_capacity(unsigned slot,bool sending) {
    (void)slot;
    (void)sending;
    return -EOPNOTSUPP;
}
int nas_lwip_socket_ip_recverr(unsigned slot,bool write,bool *enabled) {
    (void)slot;
    (void)write;
    (void)enabled;
    return -EOPNOTSUPP;
}
int nas_lwip_socket_ip_pktinfo(unsigned slot,bool write,bool *enabled) {
    (void)slot;(void)write;(void)enabled;
    return -EOPNOTSUPP;
}
int nas_lwip_socket_control_ready(unsigned slot) {
    assert(slot==LISTENER_SLOT);
    return control_pending?1:0;
}
int nas_lwip_socket_control_finish(unsigned slot) {
    assert(slot==LISTENER_SLOT && control_pending);
    control_pending=false;
    return 0;
}
int nas_lwip_socket_port_complete(unsigned slot,
                                  struct nas_lwip_bridge_result *out) {
    assert(slot==LISTENER_SLOT && out);
    if(!control_pending)return 0;
    out->status=nas_lwip_socket_control_finish(slot);
    return 1;
}
void nas_fs_close_fd(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT)return;
    unsigned slot=nas_fd_close(&test_task,(unsigned)fd);
    if(slot<NAS_OPEN_FILE_LIMIT) {
        const struct nas_file_ops *ops=nas_open_files[slot].ops;
        if(ops && ops->release)ops->release(slot);
        nas_open_files[slot]=(struct nas_open_file){0};
    }
}
/* Only the real lwIP provider may handle listener_slot. */
bool nas_unix_socket_listening(unsigned slot){(void)slot;return false;}
int nas_unix_socket_accept(unsigned slot,unsigned *out) {
    (void)slot;(void)out;abort();
}
int nas_pipe_socket_shutdown(unsigned slot,unsigned how) {
    (void)slot;(void)how;abort();
}

static unsigned live_leases(void) {
    unsigned count=0;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(nas_open_files[i].lease_token)count++;
    return count;
}
static void set_socklen(uint32_t size) {
    memcpy(user_bytes+32,&size,4);
}
static uint32_t get_socklen(void) {
    uint32_t size=0;
    memcpy(&size,user_bytes+32,4);
    return size;
}

int main(void) {
    nas_fd_system_init();
    memset(&test_task,0,sizeof(test_task));
    /* FD ownership belongs to a referenced nas_files, not a detached
     * nas_fd_table. The production lease path checks this identity. */
    test_files=nas_files_new();
    assert(test_files);
    test_task.files=test_files;
    test_task.fds=&test_files->table;
    nas_open_files[LISTENER_SLOT].refcount=1;
    nas_open_files[LISTENER_SLOT].status_flags=2;
    test_fds.entries[10]=(struct nas_fd_entry){FD_SOCKET,LISTENER_SLOT,0};
    const uint64_t addr=ACCEPT_USER_BASE;
    const uint64_t size_ptr=ACCEPT_USER_BASE+32;

    /* The same native continuation mechanism handles GETNAME, SO_REUSEADDR
     * and SHUTDOWN; usercopy is performed only after the worker finishes. */
    set_socklen(16);
    assert((int64_t)sys_ipv4_name(10,addr,size_ptr,true)==-EAGAIN);
    assert(name_pending && name_requested_peer);
    assert((int64_t)nas_fs_socket_name_description(
        LISTENER_SLOT,addr,size_ptr,true)==-EAGAIN);
    name_ready=true;
    assert(nas_fs_socket_name_description(LISTENER_SLOT,addr,size_ptr,true)==0);
    assert(get_socklen()==16 && user_bytes[0]==2 && user_bytes[1]==0);
    assert(user_bytes[2]==(uint8_t)(actual_peer.port>>8));
    assert(user_bytes[3]==(uint8_t)actual_peer.port && user_bytes[4]==127);
    uint32_t enable=1;
    memcpy(user_bytes+64,&enable,4);
    assert((int64_t)sys_ipv4_setsockopt(
        10,1,2,ACCEPT_USER_BASE+64,4)==-EAGAIN);
    assert(reuse_enabled && reuse_count==1);
    assert(nas_fs_socket_control_ready(LISTENER_SLOT));
    assert(nas_fs_socket_control_description(LISTENER_SLOT)==0);
    assert((int64_t)sys_socket_shutdown(10,2)==-EAGAIN);
    assert(shutdown_count==1 && shutdown_how==2);
    assert(nas_fs_socket_control_description(LISTENER_SLOT)==0);
    nas_task_wait_dispose(&test_task);

    /* Validate both outputs before allocating a lease/dequeuing a peer. */
    set_socklen(16);
    user_writable=false;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EFAULT);
    assert(!bridge_begin_count && !live_leases());
    user_writable=true;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,0,0)==-EFAULT);
    assert(!bridge_begin_count && !live_leases());

    /* A nonblocking listener cannot strand a task-owned lease after syscall
     * return; no queued native completion, no dangling OpenFile ref. */
    nas_open_files[LISTENER_SLOT].status_flags|=0x800;
    have_peer=false;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(!bridge_begin_count && !live_leases());
    nas_open_files[LISTENER_SLOT].status_flags=2;

    /* No arriving peer yet: the Task sleeps on listener POLLIN only. */
    have_peer=false;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(bridge_begin_count==1 && !bridge_pending && !live_leases());

    /* ACCEPT is a true Task-owned async transaction. Re-entering its native
     * continuation never creates a second lease or command. */
    have_peer=true;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0x80000ULL|0x800ULL)==-EAGAIN);
    assert(bridge_pending && bridge_begin_count==2 && live_leases()==1);
    const struct nas_fd_slot_lease first_lease=test_task.wait.accept_lease;
    assert(nas_fd_slot_lease_valid(first_lease));
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0x80000ULL|0x800ULL)==-EAGAIN);
    assert(bridge_begin_count==2 && bridge_complete_count==1);
    bridge_complete=true;
    set_socklen(5);
    uint64_t accepted=nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0x80000ULL|0x800ULL);
    assert(accepted<NAS_FD_LIMIT && !test_task.wait.accept_lease_cancel);
    assert(test_fds.entries[accepted].slot==first_lease.slot);
    assert(test_fds.entries[accepted].cloexec);
    assert(nas_open_files[first_lease.slot].status_flags&0x800ULL);
    assert(nas_open_files[first_lease.slot].ops==&accepted_ops);
    assert(!live_leases() && get_socklen()==16);
    assert(user_bytes[0]==2 && user_bytes[1]==0);
    assert(user_bytes[2]==(uint8_t)(actual_peer.port>>8));
    assert(user_bytes[3]==(uint8_t)(actual_peer.port));
    assert(user_bytes[4]==127); /* capacity=5 truncates only the address. */
    nas_fs_close_fd(accepted);
    nas_task_wait_dispose(&test_task); /* already transferred; no cancellation */
    assert(release_count==1 && bridge_cancel_count==0);

    /* A caught signal/Task exit cancels the native command first, then
     * releases its FD-slot lease; reuse receives a different token. */
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    const struct nas_fd_slot_lease interrupted=test_task.wait.accept_lease;
    assert(nas_fd_slot_lease_valid(interrupted));
    nas_task_wait_dispose(&test_task);
    assert(bridge_cancel_count==1 && !bridge_pending && !live_leases());
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(nas_fd_slot_lease_valid(test_task.wait.accept_lease));
    assert(test_task.wait.accept_lease.token!=interrupted.token);
    bridge_native_empty=true;bridge_complete=true;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(!test_task.wait.accept_lease_cancel && !live_leases());
    nas_task_wait_dispose(&test_task);
    assert(bridge_cancel_count==1);

    /* Nonblocking accept is now two-phase across syscall returns. The
     * original listening OpenFile, not the returning Task, owns the native
     * worker lease until accept() atomically publishes the accepted child. */
    bridge_native_empty=false;have_peer=true;
    nas_open_files[LISTENER_SLOT].status_flags|=0x800;
    unsigned previous_begins=bridge_begin_count;
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(prefetch_owned && bridge_pending && live_leases()==1);
    assert(bridge_begin_count==previous_begins+1);
    assert((int64_t)nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0)==-EAGAIN);
    assert(bridge_begin_count==previous_begins+1 && live_leases()==1);
    bridge_complete=true;set_socklen(16);
    accepted=nas_fs_socket_accept_description(
        LISTENER_SLOT,addr,size_ptr,0x80000ULL);
    assert(accepted<NAS_FD_LIMIT && !prefetch_owned && !bridge_pending);
    assert(test_fds.entries[accepted].cloexec && !live_leases());
    assert(!test_task.wait.accept_lease_cancel && get_socklen()==16);
    assert(user_bytes[4]==127 && user_bytes[3]==(uint8_t)actual_peer.port);
    nas_fs_close_fd(accepted);
    nas_open_files[LISTENER_SLOT].status_flags=2;

    puts("[LWIP ACCEPT SYSCALL PASS] genuine OpenFile lease, syscall usercopy,");
    puts("  blocking/EINTR and nonblocking prefetch, CLOEXEC, peer, FD ABA");
    return 0;
}
