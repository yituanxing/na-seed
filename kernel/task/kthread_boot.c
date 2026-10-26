#include <stdint.h>
#include "kthread.h"
#include "wait_queue.h"
#include "../panic.h"
static struct nas_wait_queue boot_queue;
static volatile unsigned boot_phases;
static void waiter(void *argument) {
    (void)argument;
    if(boot_phases)nas_fail("M95 waiter order");
    boot_phases=1;
    if(!nas_kthread_park(&boot_queue))
        nas_fail("M95 waiter cannot park on common queue");
    if(boot_phases!=3 || boot_queue.count)
        nas_fail("M95 waiter not resumed from real C stack");
    boot_phases=4;
}
static void producer(void *argument) {
    (void)argument;
    if(boot_phases!=1)nas_fail("M95 producer order");
    boot_phases=2;
    if(nas_wait_queue_wake(&boot_queue,1)!=1)
        nas_fail("M95 producer cannot notify sleeping worker");
    boot_phases=3;
    if(!nas_kthread_yield())
        nas_fail("M95 producer failed to yield");
    if(boot_phases!=4)nas_fail("M95 producer resumed before waiter");
    boot_phases=5;
}
/* Deliberately leave one live C continuation across the first userspace
 * syscall/timer. Only a real production scheduler entry can resume it. */
static void scheduled_after_user_entry(void *argument) {
    (void)argument;
    volatile uint64_t frame_guard=UINT64_C(0x4b74687265616435);
    if(!nas_kthread_yield())
        nas_fail("M95 deferred worker could not yield");
    if(frame_guard!=UINT64_C(0x4b74687265616435))
        nas_fail("M95 deferred worker frame corrupted");
    nas_print("[M95 KTHREAD SCHED PASS] native C frame resumed by production scheduler\n");
}
void nas_kthread_boot_selftest(void) {
    nas_wait_queue_init(&boot_queue);
    boot_phases=0;
    struct nas_kthread_handle a,b;
    if(!nas_kthread_spawn(waiter,0,&a) ||
       !nas_kthread_spawn(producer,0,&b))
        nas_fail("M95 cannot create kernel workers");
    if(nas_kthread_run(1)!=1 || boot_phases!=1 || boot_queue.count!=1)
        nas_fail("M95 kernel waiter never suspended");
    if(nas_kthread_run(1)!=1 || boot_phases!=3 || boot_queue.count)
        nas_fail("M95 kernel producer did not signal");
    if(nas_kthread_run(2)!=2 || boot_phases!=5 || nas_kthread_live())
        nas_fail("M95 suspended worker not resumed or reaped");
    nas_print("[M95 KTHREAD PASS] production worker enqueue, sleep, wake, resume, reap\n");
    struct nas_kthread_handle deferred;
    if(!nas_kthread_spawn(scheduled_after_user_entry,0,&deferred))
        nas_fail("M95 cannot create deferred native worker");
    if(nas_kthread_run(1)!=1 || nas_kthread_live()!=1)
        nas_fail("M95 deferred native worker not left runnable");
    /* NO more calls to nas_kthread_run() here: the first live user-task
     * schedule or timer-IRQ scheduler must resume this saved C frame. */
}
