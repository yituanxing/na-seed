#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/task/kthread.h"
#include "../kernel/task/k_sync.h"
#include "../kernel/task/wait_queue.h"
static uint64_t now;
static unsigned phase;
static struct nas_ksem sem;
static struct nas_kmailbox box;
static struct nas_wait_queue q;
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn))void nas_arch_halt(void){abort();}
uint64_t nas_arch_monotonic_ns(void){return now;}
static void timed_sem(void *unused) {
    (void)unused;
    phase=1;
    assert(nas_ksem_wait_until(&sem,100)==NAS_KWAIT_TIMED_OUT);
    assert(phase==2);
    phase=3;
}
static void notified_sem(void *unused) {
    (void)unused;
    phase=4;
    assert(nas_ksem_wait_until(&sem,200)==NAS_KWAIT_READY);
    assert(phase==5);
    phase=6;
}
static void timed_sender(void *unused){
    (void)unused;
    phase=7;
    assert(nas_kmailbox_send_until(&box,(void *)(uintptr_t)0xdead,300)==NAS_KWAIT_TIMED_OUT);
    assert(phase==8);
    phase=9;
}
static void timed_receiver(void *unused){
    (void)unused;
    phase=10;
    void *message=(void *)(uintptr_t)0x55;
    assert(nas_kmailbox_recv_until(&box,&message,400)==NAS_KWAIT_TIMED_OUT);
    assert(phase==11 && message==(void *)(uintptr_t)0x55);
    phase=12;
}
static void expired_immediate(void *unused){
    (void)unused;
    assert(nas_kthread_park_until(&q,now)==NAS_KWAIT_TIMED_OUT);
    phase=13;
}
static void sleeping_for_cancel(void *unused){
    (void)unused;
    assert(nas_kthread_park_until(&q,999)==NAS_KWAIT_READY);
    abort(); /* successful cancel must discard frame, not continue */
}
int main(void) {
    nas_kthread_system_init();
    nas_ksem_init(&sem,0);
    nas_kmailbox_init(&box);
    nas_wait_queue_init(&q);
    struct nas_kthread_handle x={0};
    now=10;
    assert(nas_kthread_spawn(timed_sem,0,&x));
    assert(nas_kthread_run(1)==1 && phase==1 && sem.waiters.count==1);
    assert(nas_kthread_deadlines_armed());
    assert(nas_kthread_timers_tick(99)==0 && !nas_kthread_runnable());
    now=100;
    assert(nas_kthread_timers_tick(now)==1 && sem.waiters.count==0);
    assert(!nas_kthread_deadlines_armed());
    phase=2;
    assert(nas_kthread_run(1)==1 && phase==3 && !nas_kthread_live());
    now=110;
    assert(nas_kthread_spawn(notified_sem,0,&x));
    assert(nas_kthread_run(1)==1 && phase==4 && sem.waiters.count==1);
    phase=5;
    assert(nas_ksem_post(&sem) && sem.waiters.count==0);
    assert(nas_kthread_run(1)==1 && phase==6 && !nas_kthread_deadlines_armed());
    for(unsigned i=0;i<NAS_KMAILBOX_CAP;i++)
       assert(nas_kmailbox_try_send(&box,(void *)(uintptr_t)(i+1)));
    now=220;
    assert(nas_kthread_spawn(timed_sender,0,&x));
    assert(nas_kthread_run(1)==1 && phase==7 && box.writable.count==1);
    now=300;
    assert(nas_kthread_timers_tick(now)==1 && box.writable.count==0);
    phase=8;
    assert(nas_kthread_run(1)==1 && phase==9 && box.count==NAS_KMAILBOX_CAP);
    void *got=0;
    for(unsigned i=0;i<NAS_KMAILBOX_CAP;i++) {
        assert(nas_kmailbox_try_recv(&box,&got));
        assert(got==(void *)(uintptr_t)(i+1));
    }
    now=320;
    assert(nas_kthread_spawn(timed_receiver,0,&x));
    assert(nas_kthread_run(1)==1 && phase==10 && box.readable.count==1);
    now=400;
    assert(nas_kthread_timers_tick(now)==1 && box.readable.count==0);
    phase=11;
    assert(nas_kthread_run(1)==1 && phase==12 && !nas_kthread_live());
    assert(nas_kthread_spawn(expired_immediate,0,&x));
    assert(nas_kthread_run(1)==1 && phase==13 && q.count==0);
    assert(nas_kthread_spawn(sleeping_for_cancel,0,&x));
    assert(nas_kthread_run(1)==1 && q.count==1);
    assert(nas_kthread_cancel(x) && q.count==0);
    assert(nas_kthread_timers_tick(999)==0 && !nas_kthread_deadlines_armed());
    assert(!nas_kthread_wake(x) && !nas_kthread_live());
    puts("[M97 KTIME PASS] real native C waits, timeout detach, producer-first wake, mailbox full/empty, cancel");
}
