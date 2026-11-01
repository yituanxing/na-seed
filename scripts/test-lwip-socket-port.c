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

/* This focused host executable links the actual SocketPort/bridge/official
 * lwIP, not the guest's full FD allocator. Model the same real OpenFile
 * lease identity in its fixed global array, including cancel semantics. */
static uint64_t next_test_lease=UINT64_C(0xF00001);
int nas_fd_slot_lease_begin(struct nas_fd_slot_lease *out,const char *operation) {
    (void)operation;
    if(!out)return -EINVAL;
    *out=(struct nas_fd_slot_lease){.slot=NAS_OPEN_FILE_LIMIT};
    for(unsigned slot=96;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        if(nas_open_files[slot].refcount)continue;
        const uint64_t token=++next_test_lease;
        nas_open_files[slot]=(struct nas_open_file){
            .refcount=1,.lease_token=token
        };
        *out=(struct nas_fd_slot_lease){slot,token};
        return 0;
    }
    return -ENFILE;
}
bool nas_fd_slot_lease_valid(struct nas_fd_slot_lease lease) {
    return lease.slot<NAS_OPEN_FILE_LIMIT && lease.token &&
           nas_open_files[lease.slot].refcount==1 &&
           nas_open_files[lease.slot].lease_token==lease.token;
}
bool nas_fd_slot_lease_cancel(struct nas_fd_slot_lease lease) {
    if(!nas_fd_slot_lease_valid(lease))return false;
    nas_open_files[lease.slot]=(struct nas_open_file){0};
    return true;
}

static struct nas_fd_slot_lease prefetch_ticket(void) {
    for(unsigned slot=96;slot<NAS_OPEN_FILE_LIMIT;slot++)
        if(nas_open_files[slot].lease_token)
            return (struct nas_fd_slot_lease){
                slot,nas_open_files[slot].lease_token
            };
    return (struct nas_fd_slot_lease){0};
}

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

static struct nas_lwip_bridge_id finish_prefetch(
    unsigned listener,struct nas_fd_slot_lease *lease,
    struct nas_lwip_bridge_address *peer) {
    struct nas_lwip_bridge_id accepted={0};
    for(unsigned tries=0;tries<200;tries++) {
        if(ops->poll_description(listener,1U)&1U) {
            assert(nas_lwip_socket_accept_prefetched(listener,lease,
                                                      &accepted,peer)==1);
            assert(nas_fd_slot_lease_valid(*lease));
            assert(accepted.slot==lease->slot);
            return accepted;
        }
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    abort();
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
    const uint8_t connecting_packet[]={0xCA,0xFE};
    assert(nas_lwip_socket_sendto(21,connecting_packet,
                sizeof(connecting_packet),
                (struct nas_lwip_bridge_address){0})==-EAGAIN);
    /* UDP connect has no remote handshake or deferred user completion. The
     * native worker completes peer association in the background. */
    bool udp_connected=false;
    for(unsigned tries=0;tries<300;tries++) {
        if(ops->poll_description(21,4U)&4U) {
            udp_connected=true;
            break;
        }
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    assert(udp_connected);
    named=name_slot(21,true);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38007);
    assert(nas_lwip_socket_peer_snapshot(21,&named)==-EOPNOTSUPP);
    assert(nas_lwip_socket_port_set_peer(21,lo(38007))==-EINVAL);

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

    /* Connectionless UDP retains one datagram and its actual source in the
     * same OpenFile. A multi-fragment Linux sendmsg must become exactly ONE
     * native SENDTO, never several stream writes; truncated RX drops the
     * remainder and retains the packet's real source address. */
    reserve(23,NAS_LWIP_BRIDGE_UDP);
    bool recverr=true;
    assert(nas_lwip_socket_ip_recverr(23,true,&recverr)==0);
    recverr=false;
    assert(nas_lwip_socket_ip_recverr(23,false,&recverr)==0 && recverr);
    assert(nas_lwip_socket_ip_recverr(21,false,&recverr)==0 && !recverr);
    assert(nas_lwip_socket_setbroadcast(23,true)==0);
    finish_control(23,0);
    assert(ops->poll_description(23,4U)&4U);
    assert((int64_t)ops->write_plan(23,NAS_LWIP_BRIDGE_DATA+1)==-EMSGSIZE);
    const uint8_t addressed[]="one addressed UDP packet";
    assert(nas_lwip_socket_sendto(23,addressed,sizeof(addressed)-1,
                                  lo(38007))==sizeof(addressed)-1);
    assert(nas_lwip_socket_sendto(23,addressed,sizeof(addressed)-1,
                                  lo(38007))==-EAGAIN);
    struct nas_lwip_bridge_address source={0};
    bool truncated=false;
    int64_t got=-EAGAIN;
    for(unsigned tries=0;tries<300;tries++) {
        got=nas_lwip_socket_recvfrom(20,result,sizeof(result),
                                     &source,&truncated);
        if(got>=0)break;
        assert(got==-EAGAIN);
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    assert(got==(int64_t)sizeof(addressed)-1 &&
           !memcmp(result,addressed,(size_t)got));
    assert(source.ipv4==UINT32_C(0x7f000001) && source.port);
    assert(!truncated);
    for(unsigned tries=0;tries<200 && !(ops->poll_description(23,4U)&4U);
        tries++) {
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    assert(ops->poll_description(23,4U)&4U);
    assert(nas_lwip_socket_sendto(23,addressed,sizeof(addressed)-1,
                                  lo(38007))==sizeof(addressed)-1);
    got=-EAGAIN;
    for(unsigned tries=0;tries<300;tries++) {
        got=nas_lwip_socket_recvfrom(20,result,6,&source,&truncated);
        if(got>=0)break;
        assert(got==-EAGAIN);
        test_ns+=UINT64_C(1000000);
        (void)nas_kthread_run(1);
    }
    assert(got==6 && !memcmp(result,"one ad",6) && truncated);
    assert(source.ipv4==UINT32_C(0x7f000001) && source.port);
    close_slot(23);

    reserve(30,NAS_LWIP_BRIDGE_TCP);
    assert(nas_lwip_socket_setreuseaddr(30,true)==0);
    finish_control(30,0);
    assert(nas_lwip_socket_bind(30,lo(38017))==0);
    finish_control(30,0);
    named=name_slot(30,false);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38017);
    assert(nas_lwip_socket_listen(30,3)==0);
    finish_control(30,0);
    assert(nas_lwip_socket_peer_snapshot(30,&named)==-ENOTCONN);
    assert(nas_lwip_socket_port_set_peer(30,lo(38017))==-EINVAL);
    assert((int64_t)ops->read_plan(30,1)==-ENOTCONN);
    connect_slot(31,38017);
    assert(nas_lwip_socket_peer_snapshot(31,&named)==0);
    assert(named.ipv4==UINT32_C(0x7f000001) && named.port==38017);
    assert(ops->poll_description(31,4U)&4U);

    /* The syscall layer already holds a lease for the accepted child slot.
     * Adoption transfers bridge ID onto that SAME OpenFile identity. */
    struct nas_fd_slot_lease child=lease_slot(32,UINT64_C(0x127ACCE551));
    struct nas_lwip_bridge_address peer={0};
    struct nas_lwip_bridge_id accepted=accept_slot(30,child,&peer);
    assert(accepted.slot==32);
    assert(peer.ipv4==UINT32_C(0x7f000001) && peer.port!=0);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    assert(nas_lwip_socket_peer_snapshot(32,&named)==-ENOTCONN);
    assert(nas_lwip_socket_port_set_peer(32,peer)==0);
    assert(nas_lwip_socket_port_set_peer(32,peer)==0);
    assert(nas_lwip_socket_port_set_peer(32,lo(12345))==-EALREADY);
    assert(nas_lwip_socket_peer_snapshot(32,&named)==0);
    assert(named.ipv4==peer.ipv4 && named.port==peer.port);
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

    /* Real nonblocking server path: select/poll owns a persistent lease,
     * and must not advertise POLLIN until native ACCEPT has completed.
     * A later nonblocking accept transfers this exact lease and child. */
    nas_open_files[30].status_flags|=0x800ULL;
    connect_slot(40,38017);
    struct nas_fd_slot_lease prefetched_lease={0};
    accepted=(struct nas_lwip_bridge_id){0};
    assert(nas_lwip_socket_accept_prefetched(30,&prefetched_lease,
                                            &accepted,&peer)==0);
    assert((ops->poll_description(30,1U)&1U)==0);
    struct nas_fd_slot_lease first_prefetch=prefetch_ticket();
    assert(nas_fd_slot_lease_valid(first_prefetch));
    assert(nas_lwip_socket_accept_prefetch_start(30)==0);
    assert(prefetch_ticket().token==first_prefetch.token);
    assert(nas_lwip_socket_accept_prefetched(30,&prefetched_lease,
                                            &accepted,&peer)==-EAGAIN);
    accepted=finish_prefetch(30,&prefetched_lease,&peer);
    assert(prefetched_lease.token==first_prefetch.token);
    assert(peer.ipv4==UINT32_C(0x7f000001) && peer.port!=0);
    /* The lease remains allocated while the syscall adopts the Netconn. */
    assert(prefetch_ticket().token==prefetched_lease.token);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    assert(nas_lwip_socket_port_set_peer(prefetched_lease.slot,peer)==0);
    assert(nas_lwip_socket_peer_snapshot(prefetched_lease.slot,&named)==0);
    assert(named.ipv4==peer.ipv4 && named.port==peer.port);
    nas_open_files[prefetched_lease.slot].lease_token=0;
    nas_open_files[prefetched_lease.slot].status_flags=2;
    nas_open_files[prefetched_lease.slot].ops=ops;
    assert(!prefetch_ticket().token);
    const char prefetched_payload[]="TCP after durable select accept";
    assert(ops->write(40,(const uint8_t *)prefetched_payload,
                      sizeof(prefetched_payload)-1)==sizeof(prefetched_payload)-1);
    flush_tx(40);
    assert(receive(prefetched_lease.slot,result,sizeof(result))==
           sizeof(prefetched_payload)-1);
    assert(!memcmp(result,prefetched_payload,sizeof(prefetched_payload)-1));
    close_slot(prefetched_lease.slot);
    close_slot(40);

    /* F_SETFL may flip the listener to blocking while prefetch is pending.
     * Its OpenFile-owned ticket remains harvestable and a Task must not
     * queue a competing native ACCEPT for the same connection. */
    connect_slot(41,38017);
    assert((ops->poll_description(30,1U)&1U)==0);
    struct nas_fd_slot_lease flipped_prefetch=prefetch_ticket();
    assert(nas_fd_slot_lease_valid(flipped_prefetch));
    nas_open_files[30].status_flags&=~0x800ULL;
    assert((ops->poll_description(30,1U)&1U)==0);
    struct nas_fd_slot_lease duplicate=lease_slot(42,UINT64_C(0xAA04));
    assert(nas_lwip_socket_accept_begin(30,duplicate)==-EALREADY);
    assert(nas_fd_slot_lease_cancel(duplicate));
    accepted=finish_prefetch(30,&prefetched_lease,&peer);
    assert(prefetched_lease.token==flipped_prefetch.token);
    assert(nas_lwip_socket_port_adopt(accepted)==0);
    nas_open_files[prefetched_lease.slot].lease_token=0;
    nas_open_files[prefetched_lease.slot].status_flags=2;
    nas_open_files[prefetched_lease.slot].ops=ops;
    close_slot(prefetched_lease.slot);
    close_slot(41);

    /* Closing a listening File after nas_fd_close zeroed its refcount must
     * cancel the native worker BEFORE invalidating its kernel lease. */
    reserve(50,NAS_LWIP_BRIDGE_TCP);
    assert(nas_lwip_socket_bind(50,lo(38019))==0);
    finish_control(50,0);
    assert(nas_lwip_socket_listen(50,2)==0);
    finish_control(50,0);
    nas_open_files[50].status_flags|=0x800ULL;
    connect_slot(51,38019);
    assert((ops->poll_description(50,1U)&1U)==0);
    struct nas_fd_slot_lease orphan_prefetch=prefetch_ticket();
    assert(nas_fd_slot_lease_valid(orphan_prefetch));
    nas_open_files[50].refcount=0; /* the actual final-close ordering */
    ops->release(50);
    assert(!nas_fd_slot_lease_valid(orphan_prefetch));
    assert(!nas_lwip_socket_is_slot(50));
    nas_open_files[50]=(struct nas_open_file){0};
    close_slot(51);
    for(unsigned tries=0;tries<80;tries++)(void)nas_kthread_run(1);
    assert(!prefetch_ticket().token);

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
    puts("  durable nonblocking prefetch, F_SETFL handoff, final-close cleanup");
    return 0;
}
