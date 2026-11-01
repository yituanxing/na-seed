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
        assert(nas_kthread_run(1)==1);
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
    assert(nas_lwip_socket_bind(22,lo(38027))==0);
    finish_control(22,0);
    close_slot(22);

    reserve(20,NAS_LWIP_BRIDGE_UDP);
    reserve(21,NAS_LWIP_BRIDGE_UDP);
    assert(nas_lwip_socket_bind(20,lo(38007))==0);
    /* Idempotent syscall replay must never create a second command. */
    assert(nas_lwip_socket_bind(20,lo(38007))==0);
    assert(nas_lwip_socket_bind(20,lo(38008))==-EBUSY);
    finish_control(20,0);
    assert(nas_lwip_socket_connect(21,lo(38007))==0);
    finish_control(21,0);

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
    reserve(31,NAS_LWIP_BRIDGE_TCP);
    assert(nas_lwip_socket_bind(30,lo(38017))==0);
    finish_control(30,0);
    assert(nas_lwip_socket_listen(30,3)==0);
    finish_control(30,0);
    assert(nas_lwip_socket_connect(31,lo(38017))==0);
    for(unsigned tries=0;tries<30;tries++) {
        int rc=nas_lwip_socket_control_ready(31);
        if(rc==1)break;
        assert(rc==0 && nas_kthread_run(1)==1);
    }
    int connection_rc=nas_lwip_socket_control_finish(31);
    assert(connection_rc==0 || connection_rc==-EINPROGRESS);
    if(connection_rc==-EINPROGRESS) {
        bool confirmed=false;
        for(unsigned tries=0;tries<200;tries++) {
            if(ops->poll_description(31,4U)&4U){confirmed=true;break;}
            test_ns+=UINT64_C(1000000);
            (void)nas_kthread_run(1);
        }
        assert(confirmed); /* never expose POLLOUT before worker confirms */
    }

    /* The syscall layer already holds a lease for the accepted child slot.
     * Adoption transfers bridge ID onto that SAME OpenFile identity. */
    nas_open_files[32].refcount=1;
    nas_open_files[32].status_flags=2;
    nas_open_files[32].ops=ops;
    struct nas_lwip_bridge_request accept_req={
        .op=NAS_LWIP_BRIDGE_ACCEPT,.accept_slot=32
    };
    struct nas_lwip_bridge_result accepted={0};
    for(unsigned tries=0;tries<200;tries++) {
        assert(nas_lwip_socket_port_begin(30,&accept_req)==0);
        for(unsigned waits=0;waits<10;waits++) {
            int ready=nas_lwip_socket_control_ready(30);
            assert(ready>=0);
            if(ready==1)break;
            (void)nas_kthread_run(1);
        }
        int status=nas_lwip_socket_port_complete(30,&accepted);
        if(status==0)continue;
        assert(status==1);
        if(accepted.status==0)break;
        assert(accepted.status==-EAGAIN);
        test_ns+=UINT64_C(1000000);
    }
    assert(accepted.status==0 && accepted.accepted.slot==32);
    assert(nas_lwip_socket_port_adopt(accepted.accepted)==0);
    assert(nas_lwip_socket_is_slot(32));

    const char tcp[]="TCP stream through FileOps";
    assert(ops->write_plan(32,sizeof(tcp)-1)==sizeof(tcp)-1);
    assert(ops->write(32,(const uint8_t *)tcp,sizeof(tcp)-1)==sizeof(tcp)-1);
    flush_tx(32);
    assert(receive(31,result,6)==6);
    assert(!memcmp(result,"TCP st",6));
    assert(receive(31,result,sizeof(result))==sizeof(tcp)-1-6);
    assert(!memcmp(result,tcp+6,sizeof(tcp)-1-6));

    /* No user Task invokes lwIP Netconn teardown on final File release. */
    close_slot(32);
    close_slot(31);
    close_slot(30);
    close_slot(21);
    close_slot(20);
    for(unsigned tries=0;tries<200;tries++) {
        if(!nas_kthread_runnable())break;
        (void)nas_kthread_run(1);
    }
    puts("[LWIP SOCKET PORT PASS] real native UDP/TCP, async FileOps,") ;
    puts("  OpenFile identity, copied queued writes, poll/read, accept adoption");
    return 0;
}
