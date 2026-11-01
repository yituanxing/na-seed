/* Real upstream lwIP native FileOps proof: UDP/TCP, async OPEN/control,
 * kernel-copied writes, read prefetch/poll, accept adoption and stale close.
 * No Fake Netconn, no second FD namespace and no network syscalls here. */
#include "../ports/lwip/socket_port.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/task/kthread.h"
#include "../kernel/errno.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint64_t test_ns=1;
uint64_t nas_arch_monotonic_ns(void){return test_ns;}
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
void *nas_heap_calloc(size_t n,size_t size){return calloc(n,size);}
bool nas_heap_free(void *p){free(p);return true;}
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
struct nas_task *nas_current_task;

static unsigned ready_calls;
static void core_ready(void *unused){(void)unused;ready_calls++;}
static struct nas_lwip_bridge_address lo(unsigned port){
    return (struct nas_lwip_bridge_address){
        .ipv4=UINT32_C(0x7f000001),.port=(uint16_t)port
    };
}
static const struct nas_file_ops *ops;
static void reserve(unsigned slot,enum nas_lwip_bridge_type type) {
    assert(slot<NAS_OPEN_FILE_LIMIT);
    assert(!nas_open_files[slot].refcount);
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].status_flags=2; /* O_RDWR */
    nas_open_files[slot].ops=ops;
    assert(nas_lwip_socket_open(slot,type)==0);
    for(unsigned tries=0;tries<100;tries++){
        int rc=nas_lwip_socket_open_ready(slot);
        if(rc==1)return;
        assert(rc==0 && nas_kthread_run(1)==1);
    }
    abort();
}
static void finish_control(unsigned slot,int expected) {
    for(unsigned tries=0;tries<200;tries++) {
        int rc=nas_lwip_socket_control_ready(slot);
        assert(rc>=0);
        if(rc==1) {
            assert(nas_lwip_socket_control_finish(slot)==expected);
            return;
        }
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static void connect_slot(unsigned slot,unsigned port) {
    reserve(slot,NAS_LWIP_BRIDGE_TCP);
    assert(nas_lwip_socket_connect(slot,lo(port))==0);
    for(unsigned tries=0;tries<300;tries++) {
        int ready=nas_lwip_socket_control_ready(slot);
        assert(ready>=0);
        if(ready==1) {
            assert(nas_lwip_socket_control_finish(slot)==0);
            return;
        }
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static struct nas_lwip_bridge_address name_slot(unsigned slot,bool peer) {
    assert(nas_lwip_socket_getname_begin(slot,peer)==0);
    struct nas_lwip_bridge_address address={0};
    for(unsigned tries=0;tries<100;tries++) {
        int rc=nas_lwip_socket_getname_complete(slot,&address);
        if(rc==1)return address;
        assert(rc==0);
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static void listener_ready(unsigned slot) {
    for(unsigned tries=0;tries<300;tries++) {
        if(ops->poll_description(slot,1U)&1U)return;
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static struct nas_fd_slot_lease lease_slot(unsigned slot,uint64_t token) {
    assert(slot<NAS_OPEN_FILE_LIMIT && token && !nas_open_files[slot].refcount);
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].lease_token=token;
    nas_open_files[slot].status_flags=2;
    nas_open_files[slot].ops=ops;
    return (struct nas_fd_slot_lease){slot,token};
}
static struct nas_lwip_bridge_id accept_slot(unsigned listener,
                                             struct nas_fd_slot_lease lease,
                                             struct nas_lwip_bridge_address *peer) {
    listener_ready(listener);
    assert(nas_lwip_socket_accept_begin(listener,lease)==0);
    assert(nas_lwip_socket_accept_begin(listener,lease)==-EALREADY);
    /* Once a native ACCEPT is running, a still-nonempty lwIP backlog must
     * not busy-wake the blocked Task before its actual result is ready. */
    assert((ops->poll_description(listener,1U)&1U)==0);
    struct nas_lwip_bridge_id accepted={0};
    for(unsigned tries=0;tries<200;tries++) {
        int rc=nas_lwip_socket_accept_complete(listener,lease,&accepted,peer);
        if(rc==1)return accepted;
        assert(rc==0);
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static void flush_tx(unsigned slot){
    for(unsigned tries=0;tries<200;tries++) {
        uint16_t ready=ops->poll_description(slot,4U);
        assert(!(ready&8U));
        if((ready&4U)!=0)return;
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static unsigned receive(unsigned slot,uint8_t *dst,unsigned amount) {
    for(unsigned tries=0;tries<300;tries++) {
        uint64_t budget=ops->read_plan(slot,amount);
        if((int64_t)budget>=0) {
            assert(budget<=amount);
            assert(ops->read(slot,dst,budget)==budget);
            return (unsigned)budget;
        }
        assert((int64_t)budget==-EOPNOTSUPP);
        /* The scheduler queries poll_description on every wait scan,
         * which re-probes an idle native Netconn after a bounded delay. */
        (void)ops->poll_description(slot,1U);
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
}
static void close_slot(unsigned slot) {
    assert(nas_lwip_socket_is_slot(slot));
    ops->release(slot);
    assert(!nas_lwip_socket_is_slot(slot));
    nas_open_files[slot]=(struct nas_open_file){0};
}

int main(void) {
    nas_kthread_system_init();
    sys_init();
    tcpip_init(core_ready,NULL);
    assert(nas_lwip_bridge_start());
    assert(nas_kthread_run(1)==1 && ready_calls==1);
    ops=nas_lwip_socket_port_ops();
    assert(ops && ops->read && ops->write && ops->poll_description &&
           ops->release && ops->sends_sigpipe && ops->io_wait_flags==
                   (NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY));

    /* Immediate control submission after asynchronous OPEN is queued in
     * the File owner, without blocking in the creating Task. */
    nas_open_files[22].refcount=1;
    nas_open_files[22].status_flags=2;
    nas_open_files[22].ops=ops;
    assert(nas_lwip_socket_open(22,NAS_LWIP_BRIDGE_UDP)==0);
    assert(nas_lwip_socket_bind(22,lo(38027))==0);
    assert(nas_lwip_socket_bind(22,lo(38027))==-EALREADY);
    finish_control(22,0);
    close_slot(22);

    reserve(20,NAS_LWIP_BRIDGE_UDP);
    reserve(21,NAS_LWIP_BRIDGE_UDP);
    assert(nas_lwip_socket_setreuseaddr(20,true)==0);
    finish_control(20,0);
    assert(nas_lwip_socket_bind(20,lo(38007))==0);
    /* A second Task cannot wait on another Task's one-shot completion. */
    assert(nas_lwip_socket_bind(20,lo(38007))==-EALREADY);
    assert(nas_lwip_socket_bind(20,lo(38008))==-EBUSY);
    finish_control(20,0);
    struct nas_lwip_bridge_address named=name_slot(20,false);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38007);
    assert(nas_lwip_socket_connect(21,lo(38007))==0);
    finish_control(21,0);
    named=name_slot(21,true);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38007);

    char payload[]="copied native UDP payload";
    const unsigned length=(unsigned)sizeof(payload)-1;
    assert(ops->write_plan(21,length)==length);
    assert(ops->write(21,(const uint8_t *)payload,length)==length);
    memset(payload,'X',length); /* provider owns TX queue at return */
    assert((int64_t)ops->write_plan(21,1)==-EOPNOTSUPP);
    flush_tx(21);
    uint8_t result[128]={0};
    assert(receive(20,result,sizeof(result))==length);
    assert(!memcmp(result,"copied native UDP payload",length));
    assert(ops->poll_description(20,1U)==0);

    const char second[]="truncated-datagram";
    assert(ops->write(21,(const uint8_t *)second,sizeof(second)-1)==
           sizeof(second)-1);
    flush_tx(21);
    assert(receive(20,result,5)==5);
    assert(!memcmp(result,"trunc",5));
    /* UDP discards the rest of the first datagram, not a TCP-style tail. */
    assert((int64_t)ops->read_plan(20,1)==-EOPNOTSUPP);

    reserve(30,NAS_LWIP_BRIDGE_TCP);
    assert(nas_lwip_socket_setreuseaddr(30,true)==0);
    finish_control(30,0);
    assert(nas_lwip_socket_bind(30,lo(38017))==0);
    finish_control(30,0);
    named=name_slot(30,false);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38017);
    assert(nas_lwip_socket_listen(30,3)==0);
    finish_control(30,0);
    assert((int64_t)ops->read_plan(30,1)==-ENOTCONN);
    connect_slot(31,38017);
    assert(ops->poll_description(31,4U)&4U);

    /* The syscall layer already holds a lease for the accepted child slot.
     * Adoption transfers bridge ID onto that SAME OpenFile identity. */
    struct nas_fd_slot_lease child=lease_slot(32,UINT64_C(0x127ACCE551));
    struct nas_lwip_bridge_address peer={0};
    struct nas_lwip_bridge_id accepted=accept_slot(30,child,&peer);
    assert(accepted.slot==32);
    assert(peer.ipv4==UINT32_C(0x7f000001) && peer.port!=0);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    nas_open_files[32].lease_token=0; /* fixture's atomic lease commit */
    assert(nas_lwip_socket_is_slot(32));
    named=name_slot(32,true);
    assert(named.ipv4==peer.ipv4 && named.port==peer.port);

    const char tcp[]="TCP stream through FileOps";
    assert(ops->write_plan(32,sizeof(tcp)-1)==sizeof(tcp)-1);
    assert(ops->write(32,(const uint8_t *)tcp,sizeof(tcp)-1)==sizeof(tcp)-1);
    /* Caller writes copied bytes then immediately shuts down sending.
     * FIN must wait behind the TX queue, never truncate its last segment. */
    assert(nas_lwip_socket_shutdown(32,1)==0);
    assert(nas_lwip_socket_shutdown(32,1)==-EALREADY);
    assert((int64_t)ops->write_plan(32,1)==-EPIPE);
    assert((ops->poll_description(32,4U)&4U)==0);
    finish_control(32,0);
    assert((int64_t)ops->write_plan(32,1)==-EPIPE);
    assert((ops->poll_description(32,4U)&4U)==0);
    assert(receive(31,result,6)==6);
    assert(!memcmp(result,"TCP st",6));
    /* GETNAME must not reject a File just because an unread TCP tail is
     * cached in the same provider, as ncat may inspect address midstream. */
    named=name_slot(31,false);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port!=0);
    assert(receive(31,result,sizeof(result))==sizeof(tcp)-1-6);
    assert(!memcmp(result,tcp+6,sizeof(tcp)-1-6));
    assert(receive(31,result,1)==0); /* real upstream FIN after queued TX */

    /* An interrupted accept cancels its lease BEFORE releasing that slot.
     * A queued native completion must drain even when the original Task
     * will never call accept_complete again. The same pending connection
     * can be accepted by a later Task using a fresh lease token. */
    close_slot(32);
    close_slot(31);
    connect_slot(33,38017);
    struct nas_fd_slot_lease canceled=lease_slot(34,UINT64_C(0xAA00));
    listener_ready(30);
    assert(nas_lwip_socket_accept_begin(30,canceled)==0);
    assert(nas_lwip_socket_accept_cancel(30,canceled)==0);
    nas_open_files[34]=(struct nas_open_file){0};
    for(unsigned tries=0;tries<200;tries++) {
        if(ops->poll_description(30,1U)&1U)break;
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    struct nas_fd_slot_lease retry=lease_slot(35,UINT64_C(0xAA01));
    accepted=accept_slot(30,retry,&peer);
    assert(accepted.slot==35);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    nas_open_files[35].lease_token=0;
    close_slot(35);
    close_slot(33);

    /* Cancellation after the native worker accepted a child, but before
     * userspace claimed it, must explicitly close that orphan child: the
     * bridge result has already been taken into SocketPort's cache. */
    connect_slot(36,38017);
    struct nas_fd_slot_lease late=lease_slot(37,UINT64_C(0xAA02));
    listener_ready(30);
    assert(nas_lwip_socket_accept_begin(30,late)==0);
    bool completed=false;
    for(unsigned tries=0;tries<100;tries++) {
        if(nas_lwip_socket_control_ready(30)==1){completed=true;break;}
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    assert(completed);
    assert(nas_lwip_socket_accept_cancel(30,late)==0);
    nas_open_files[37]=(struct nas_open_file){0};
    close_slot(36);
    for(unsigned tries=0;tries<40;tries++)(void)nas_kthread_run(1);
    connect_slot(38,38017);
    struct nas_fd_slot_lease after_cancel=lease_slot(39,UINT64_C(0xAA03));
    accepted=accept_slot(30,after_cancel,&peer);
    assert(accepted.slot==39);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    nas_open_files[39].lease_token=0;
    close_slot(39);
    close_slot(38);

    /* No user Task invokes lwIP Netconn teardown on final File release. */
    close_slot(30);
    close_slot(21);
    close_slot(20);
    for(unsigned tries=0;tries<200;tries++) {
        if(!nas_kthread_runnable())break;
        (void)nas_kthread_run(1);
    }
    puts("[LWIP SOCKET PORT PASS] real native UDP/TCP, async FileOps,") ;
    puts("  OpenFile identity, copied TX, poll/read, leased accept/cancel/reaccept");
    return 0;
}
