/* Genuine common kernel wait-queue implementation: FIFO, cancellation,
 * reentrant enqueue, wake budget, no stale callback after task teardown. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task/wait_queue.h"

static struct nas_wait_queue q;
static struct nas_wait_entry a,b,c;
static unsigned logbuf[12], logs;
static bool rearm_a;
static void noticed(struct nas_wait_entry *entry,void *opaque) {
    unsigned id=(unsigned)(uintptr_t)opaque;
    assert(!entry->queue && logs<12);
    logbuf[logs++]=id;
    if(id==1 && rearm_a) {
        rearm_a=false;
        assert(nas_wait_queue_enqueue(&q,entry,noticed,(void *)(uintptr_t)id));
    }
}
int main(void) {
    nas_wait_queue_init(&q);
    nas_wait_entry_reset(&a);
    nas_wait_entry_reset(&b);
    nas_wait_entry_reset(&c);
    assert(!nas_wait_queue_enqueue(0,&a,noticed,0));
    assert(!nas_wait_queue_enqueue(&q,&a,0,0));
    assert(nas_wait_queue_enqueue(&q,&a,noticed,(void *)(uintptr_t)1));
    assert(nas_wait_queue_enqueue(&q,&b,noticed,(void *)(uintptr_t)2));
    assert(nas_wait_queue_enqueue(&q,&c,noticed,(void *)(uintptr_t)3));
    assert(q.count==3 && !nas_wait_queue_enqueue(&q,&a,noticed,0));
    nas_wait_entry_reset(&b); /* queued reset must be rejected */
    assert(b.queue==&q && q.count==3);
    assert(nas_wait_queue_remove(&b) && !nas_wait_queue_remove(&b));
    assert(q.count==2 && q.first==&a && q.last==&c);
    rearm_a=true;
    assert(nas_wait_queue_wake(&q,0)==2);
    assert(logs==2 && logbuf[0]==1 && logbuf[1]==3);
    assert(q.count==1 && q.first==&a && q.last==&a);
    assert(nas_wait_queue_wake(&q,1)==1 && logbuf[2]==1);
    assert(!q.count && !q.first && !q.last);
    assert(nas_wait_queue_wake(&q,0)==0);
    assert(nas_wait_queue_enqueue(&q,&a,noticed,(void *)(uintptr_t)1));
    assert(nas_wait_queue_enqueue(&q,&b,noticed,(void *)(uintptr_t)2));
    assert(nas_wait_queue_enqueue(&q,&c,noticed,(void *)(uintptr_t)3));
    assert(nas_wait_queue_wake(&q,1)==1 && logbuf[3]==1);
    assert(nas_wait_queue_remove(&b)); /* exiting waiter */
    assert(nas_wait_queue_wake(&q,0)==1 && logbuf[4]==3);
    assert(logs==5 && q.count==0);
    puts("[M92 WAIT PASS] FIFO, batch isolation, rearm, cancel, limit and detached teardown");
    return 0;
}
