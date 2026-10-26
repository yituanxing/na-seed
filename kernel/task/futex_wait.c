#include <stdint.h>
#include <stdbool.h>
#include "futex_wait.h"
#include "wait_queue.h"
#include "run_state.h"
#include "../task.h"
#include "../syscall/ops.h"

/* Stable intrusive waiter identity, per-MM address keys. Hash collision
 * filtering prevents cross-process wake on the same user virtual address. */
#define FUTEX_BUCKETS 64U
static struct nas_wait_queue queues[FUTEX_BUCKETS];
static unsigned hash(const struct nas_mm *mm,uint64_t addr) {
    uint64_t key=((uint64_t)(uintptr_t)mm>>4)^(addr>>2);
    key^=key>>16;key^=key>>32;
    return (unsigned)(key&(FUTEX_BUCKETS-1U));
}
static void notify(struct nas_wait_entry *e,void *opaque) {
    struct nas_task *t=opaque;
    if(t && &t->kernel_wait==e) {
        t->wait.futex_woken=true;
        (void)nas_task_wake_ready(t);
    }
}
bool nas_futex_wait_arm(struct nas_task *t) {
    if(!t || !t->mm || !t->wait.futex_mask ||
       t->wait.futex_woken || t->kernel_wait.queue)return false;
    return nas_wait_queue_enqueue(&queues[hash(t->mm,t->wait.futex_addr)],
                                  &t->kernel_wait,notify,t);
}
void nas_futex_wait_cancel(struct nas_task *t) {
    if(t)(void)nas_wait_queue_remove(&t->kernel_wait);
}
struct key {struct nas_mm *mm;uint64_t addr;uint32_t mask;};
static bool matches(const struct nas_wait_entry *e,void *opaque) {
    const struct key *k=opaque;
    const struct nas_task *t=e->opaque;
    return t && t->mm==k->mm && t->wait.futex_addr==k->addr &&
        t->state==NAS_TASK_BLOCKED && t->wait.pending_valid &&
        t->wait.blocked_op==NAS_SC_futex && !t->wait.futex_woken &&
        (t->wait.futex_mask&k->mask)!=0;
}
unsigned nas_futex_wait_wake(struct nas_mm *mm,uint64_t addr,
                            unsigned max,uint32_t mask) {
    if(!mm || !max || !mask)return 0;
    struct key k={mm,addr,mask};
    return nas_wait_queue_wake_if(&queues[hash(mm,addr)],max,matches,&k);
}
unsigned nas_futex_wait_requeue(struct nas_mm *mm,uint64_t from,
                               uint64_t to,unsigned max) {
    if(!mm || !max || from==to)return 0;
    struct nas_wait_queue *src=&queues[hash(mm,from)];
    struct nas_wait_queue *dst=&queues[hash(mm,to)];
    struct key k={mm,from,UINT32_MAX};
    unsigned visited=src->count,moved=0;
    struct nas_wait_entry *e=src->first;
    /* Limit to the original queue length, including when src == dst.
     * Requeued nodes append to the tail and must not transfer twice. */
    while(e && visited-- && moved<max) {
        struct nas_wait_entry *next=e->next;
        if(matches(e,&k)) {
            struct nas_task *t=e->opaque;
            (void)nas_wait_queue_remove(e);
            t->wait.futex_addr=to;
            if(!nas_wait_queue_enqueue(dst,e,notify,t)) {
                t->wait.futex_addr=from;
                (void)nas_wait_queue_enqueue(src,e,notify,t);
                break;
            }
            ++moved;
        }
        e=next;
    }
    return moved;
}
