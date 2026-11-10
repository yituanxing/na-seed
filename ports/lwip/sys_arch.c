/* SPDX-License-Identifier: BSD-3-Clause
 * Native sys_arch for upstream lwIP 2.2.1: only adaptation to existing
 * NaSeed kernel threads, semaphores, mailboxes, heap and monotonic clock. */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "../../kernel/task/k_sync.h"
#include "../../kernel/task/kthread.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/arch/arch.h"
#include "../../kernel/time/clock.h"
#include "../../kernel/panic.h"
#include <stddef.h>
#include <stdint.h>

struct nas_lwip_sem { struct nas_ksem native; };
struct nas_lwip_mbox { struct nas_kmailbox native; };

/* Network stack memory allocation and sys_* calls must not be executed from
 * IRQ context; only trypost is potentially IRQ-safe. The bootstrap kernel
 * currently executes C with local IRQs masked and one CPU. Before making
 * it preemptible/SMP, protect these and the native queues with irqsave locks. */
sys_prot_t sys_arch_protect(void){return 0;}
void sys_arch_unprotect(sys_prot_t state){(void)state;}
void nas_lwip_assert_failed(const char *reason){
 nas_fail(reason?reason:"lwIP assertion");
}
/* Not cryptographic entropy. Before exposing TCP on untrusted networks,
 * seed a CSPRNG from a real system entropy source for TCP ISN hardening. */
uint32_t nas_lwip_random32(void){
 static uint64_t state;
 if(!state)state=nas_arch_monotonic_ns()|UINT64_C(0x9e3779b97f4a7c15);
 state^=state>>12;state^=state<<25;state^=state>>27;
 return (uint32_t)((state*UINT64_C(2685821657736338717))>>32);
}
static uint64_t deadline_ms(u32_t timeout){
 if(!timeout)return 0;
 return nas_time_add_deadline(nas_arch_monotonic_ns(),
                              (uint64_t)timeout*UINT64_C(1000000));
}
static u32_t elapsed_ms(uint64_t start){
 uint64_t now=nas_arch_monotonic_ns();
 if(now<=start)return 0;
 uint64_t elapsed=(now-start)/UINT64_C(1000000);
 return elapsed>=SYS_ARCH_TIMEOUT ? SYS_ARCH_TIMEOUT-1U:(u32_t)elapsed;
}
void sys_init(void){}
u32_t sys_now(void){return (u32_t)(nas_arch_monotonic_ns()/UINT64_C(1000000));}
u32_t sys_jiffies(void){return sys_now();}
err_t sys_sem_new(sys_sem_t *sem,u8_t count){
 if(!sem)return ERR_ARG;
 *sem=NULL;
 struct nas_lwip_sem *s=nas_heap_calloc(1,sizeof(*s));
 if(!s)return ERR_MEM;
 nas_ksem_init(&s->native,count);
 *sem=s;
 return ERR_OK;
}
void sys_sem_signal(sys_sem_t *sem){
 if(!sys_sem_valid(sem) || !nas_ksem_post(&(*sem)->native))
     nas_lwip_assert_failed("lwIP semaphore signal invalid or overflow");
}
u32_t sys_arch_sem_wait(sys_sem_t *sem,u32_t timeout){
 if(!sys_sem_valid(sem))nas_lwip_assert_failed("lwIP semaphore wait invalid");
 uint64_t start=nas_arch_monotonic_ns();
 enum nas_kwait_result result=nas_ksem_wait_until(&(*sem)->native,
                                                  deadline_ms(timeout));
 if(result==NAS_KWAIT_INVALID)
     nas_lwip_assert_failed("lwIP semaphore cannot park this context");
 return result==NAS_KWAIT_READY?elapsed_ms(start):SYS_ARCH_TIMEOUT;
}
void sys_sem_free(sys_sem_t *sem){
 if(!sys_sem_valid(sem) || (*sem)->native.waiters.count)
     nas_lwip_assert_failed("lwIP semaphore destroyed with live waiters");
 if(!nas_heap_free(*sem))nas_lwip_assert_failed("lwIP semaphore free failed");
 *sem=NULL;
}
int sys_sem_valid(sys_sem_t *sem){return sem && *sem!=NULL;}
void sys_sem_set_invalid(sys_sem_t *sem){if(sem)*sem=NULL;}

err_t sys_mbox_new(sys_mbox_t *box,int size){
 if(!box)return ERR_ARG;
 *box=NULL;
 if(size<0 || size>(int)NAS_KMAILBOX_CAP)return ERR_MEM;
 struct nas_lwip_mbox *b=nas_heap_calloc(1,sizeof(*b));
 if(!b)return ERR_MEM;
 nas_kmailbox_init(&b->native);
 *box=b;
 return ERR_OK;
}
void sys_mbox_post(sys_mbox_t *box,void *message){
 if(!sys_mbox_valid(box) || !nas_kmailbox_send(&(*box)->native,message))
     nas_lwip_assert_failed("lwIP mailbox post cannot park this context");
}
err_t sys_mbox_trypost(sys_mbox_t *box,void *message){
 return sys_mbox_valid(box) &&
        nas_kmailbox_try_send(&(*box)->native,message)?ERR_OK:ERR_MEM;
}
err_t sys_mbox_trypost_fromisr(sys_mbox_t *box,void *message){
 return sys_mbox_trypost(box,message);
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *box,void **message,u32_t timeout){
 if(!sys_mbox_valid(box))nas_lwip_assert_failed("lwIP mailbox fetch invalid");
 void *value=NULL;
 uint64_t start=nas_arch_monotonic_ns();
 enum nas_kwait_result result=nas_kmailbox_recv_until(&(*box)->native,&value,
                                                      deadline_ms(timeout));
 if(result==NAS_KWAIT_INVALID)
     nas_lwip_assert_failed("lwIP mailbox cannot park this context");
 if(result!=NAS_KWAIT_READY)return SYS_ARCH_TIMEOUT;
 if(message)*message=value;
 return elapsed_ms(start);
}
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *box,void **message){
 if(!sys_mbox_valid(box))return SYS_MBOX_EMPTY;
 void *value=NULL;
 if(!nas_kmailbox_try_recv(&(*box)->native,&value))return SYS_MBOX_EMPTY;
 if(message)*message=value;
 return 0;
}
void sys_mbox_free(sys_mbox_t *box){
 if(!sys_mbox_valid(box) || (*box)->native.count ||
    (*box)->native.readable.count || (*box)->native.writable.count)
     nas_lwip_assert_failed("lwIP mailbox destroyed with live messages/waiters");
 if(!nas_heap_free(*box))nas_lwip_assert_failed("lwIP mailbox free failed");
 *box=NULL;
}
int sys_mbox_valid(sys_mbox_t *box){return box && *box!=NULL;}
void sys_mbox_set_invalid(sys_mbox_t *box){if(box)*box=NULL;}

sys_thread_t sys_thread_new(const char *name,lwip_thread_fn thread,void *arg,
                            int stacksize,int prio){
 (void)name;(void)prio;
 sys_thread_t handle={0};
 if(!thread || stacksize<0 || stacksize>(int)NAS_KTHREAD_STACK_BYTES ||
    !nas_kthread_spawn(thread,arg,&handle))
     nas_lwip_assert_failed("lwIP kernel worker unavailable or stack too small");
 return handle;
}
