#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/task/kthread.h"
#include "../kernel/task/wait_queue.h"
void nas_print(const char *s){fputs(s,stdout);}
__attribute__((noreturn)) void nas_arch_halt(void){abort();}
static struct nas_wait_queue q;
static unsigned seen;
static void a(void *unused) {
    (void)unused;
    seen=1;
    assert(nas_kthread_park(&q));
    assert(seen==3);
    seen=4;
}
static void b(void *unused) {
    (void)unused;
    assert(seen==1);
    seen=2;
    assert(nas_wait_queue_wake(&q,1)==1);
    seen=3;
    assert(nas_kthread_yield());
    assert(seen==4);
    seen=5;
}
static void once(void *unused){(void)unused;seen++;}
int main(void) {
    nas_kthread_system_init();
    nas_wait_queue_init(&q);
    assert(!nas_kthread_park(&q));
    struct nas_kthread_handle x={0},y={0},reused={0};
    assert(!nas_kthread_spawn(0,0,&x));
    assert(nas_kthread_spawn(a,0,&x));
    assert(nas_kthread_spawn(b,0,&y));
    assert(nas_kthread_live()==2);
    assert(nas_kthread_run(1)==1 && seen==1 && q.count==1);
    assert(!nas_kthread_wake(0));
    assert(nas_kthread_run(1)==1 && seen==3 && q.count==0);
    assert(!nas_kthread_wake(x)); /* producer already notified */
    assert(nas_kthread_run(2)==2 && seen==5 && !nas_kthread_live());
    seen=0;
    assert(!nas_kthread_wake(x)); /* stale: the waiter already returned */
    assert(!nas_kthread_cancel(x));
    assert(nas_kthread_spawn(once,0,&reused));
    assert(reused.generation && !nas_kthread_wake(x));
    assert(nas_kthread_run(1)==1 && seen==1 && !nas_kthread_live());
    assert(nas_kthread_spawn(a,0,&reused));
    assert(nas_kthread_run(1)==1 && q.count==1);
    assert(!nas_kthread_wake(x) && !nas_kthread_cancel(x));
    assert(nas_kthread_cancel(reused) && q.count==0);
    assert(!nas_kthread_cancel(reused));
    nas_kthread_system_init();
    puts("[M95 HOST PASS] native x86 C frames, common queue, yield, stale-handle isolation, cancel and slot reuse");
}
