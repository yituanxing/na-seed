#include "kthread.h"
#include "kernel_context.h"
#include "wait_queue.h"
#include "../panic.h"
#include <stdint.h>
#include <stdbool.h>
/* No heap allocation, stack memcpy, floating point or user MM switching.
 * Keep this deliberately separate from Linux user-task accounting while
 * per-task native syscall continuation migration remains incomplete. */
#define NAS_KTHREAD_LIMIT 4U
#define NAS_KTHREAD_STACK 8192U
enum worker_state { KTHREAD_FREE, KTHREAD_READY, KTHREAD_RUNNING,
                    KTHREAD_WAITING, KTHREAD_DONE };
struct nas_kthread {
    struct nas_kernel_context context;
    struct nas_wait_entry waiting;
    void (*entry)(void *);
    void *argument;
    enum worker_state state;
    uint64_t generation;
    _Alignas(16) uint8_t stack[NAS_KTHREAD_STACK];
};
static struct nas_kthread workers[NAS_KTHREAD_LIMIT];
static struct nas_kthread *active;
static struct nas_kernel_context caller_context;
static bool in_dispatch;
static unsigned next_slot;
static void kthread_start(void) {
    struct nas_kthread *self=active;
    if(!self || self->state!=KTHREAD_RUNNING || !self->entry)
        nas_fail("M95 kernel worker trampoline without active owner");
    self->entry(self->argument);
    if(self->waiting.queue)
        nas_fail("M95 worker returned while still queued");
    self->state=KTHREAD_DONE;
    nas_arch_kernel_swap(&self->context,&caller_context);
    nas_fail("M95 returned to finished kernel worker");
}
void nas_kthread_system_init(void) {
    if(in_dispatch || active)
        nas_fail("M95 cannot reinitialize running kernel workers");
    for(unsigned i=0;i<NAS_KTHREAD_LIMIT;i++) {
        if(workers[i].waiting.queue)
            nas_fail("M95 cannot reinitialize armed kernel worker");
        workers[i].state=KTHREAD_FREE;
        nas_kernel_context_reset(&workers[i].context);
        nas_wait_entry_reset(&workers[i].waiting);
        workers[i].entry=0;
        workers[i].argument=0;
    }
    nas_kernel_context_reset(&caller_context);
    next_slot=0;
}
bool nas_kthread_spawn(void (*run)(void *),void *argument,
                       struct nas_kthread_handle *handle) {
    if(!run || !handle || active)return false; /* must not race active worker */
    *handle=(struct nas_kthread_handle){0};
    for(unsigned i=0;i<NAS_KTHREAD_LIMIT;i++) {
        struct nas_kthread *w=&workers[i];
        if(w->state!=KTHREAD_FREE)continue;
        nas_wait_entry_reset(&w->waiting);
        if(w->generation==UINT64_MAX)return false; /* fail closed on reuse wrap */
        if(!nas_kernel_context_prepare(&w->context,w->stack,
                                       sizeof(w->stack),kthread_start))
            return false;
        w->generation++;
        w->entry=run;
        w->argument=argument;
        w->state=KTHREAD_READY;
        handle->slot=i;
        handle->generation=w->generation;
        return true;
    }
    return false;
}
static void return_to_dispatch(void) {
    struct nas_kthread *self=active;
    if(!self || !in_dispatch)nas_fail("M95 kernel worker outside dispatcher");
    nas_arch_kernel_swap(&self->context,&caller_context);
    if(active!=self || self->state!=KTHREAD_RUNNING)
        nas_fail("M95 invalid kernel worker resumption");
}
bool nas_kthread_yield(void) {
    if(!active || active->state!=KTHREAD_RUNNING)return false;
    active->state=KTHREAD_READY;
    return_to_dispatch();
    return true;
}
static void queued_wakeup(struct nas_wait_entry *entry,void *argument) {
    struct nas_kthread *w=argument;
    if(!w || entry!=&w->waiting || entry->queue ||
       w->state!=KTHREAD_WAITING)
        nas_fail("M95 invalid kernel worker wake ownership");
    w->state=KTHREAD_READY;
}
bool nas_kthread_park(struct nas_wait_queue *queue) {
    struct nas_kthread *self=active;
    if(!self || self->state!=KTHREAD_RUNNING || !queue)return false;
    if(!nas_wait_queue_enqueue(queue,&self->waiting,queued_wakeup,self))
        return false;
    self->state=KTHREAD_WAITING;
    return_to_dispatch();
    if(self->waiting.queue)nas_fail("M95 resumed queued kernel worker");
    return true;
}
static struct nas_kthread *handle_lookup(struct nas_kthread_handle h) {
    if(h.slot>=NAS_KTHREAD_LIMIT || !h.generation)return 0;
    struct nas_kthread *w=&workers[h.slot];
    return w->generation==h.generation ? w : 0;
}
bool nas_kthread_wake(struct nas_kthread_handle handle) {
    struct nas_kthread *w=handle_lookup(handle);
    if(!w || w->state!=KTHREAD_WAITING)return false;
    (void)nas_wait_queue_remove(&w->waiting);
    w->state=KTHREAD_READY;
    return true;
}
bool nas_kthread_cancel(struct nas_kthread_handle handle) {
    struct nas_kthread *w=handle_lookup(handle);
    if(!w || (w->state!=KTHREAD_WAITING && w->state!=KTHREAD_READY))
        return false;
    (void)nas_wait_queue_remove(&w->waiting);
    nas_kernel_context_reset(&w->context);
    w->entry=0;
    w->argument=0;
    w->state=KTHREAD_FREE;
    return true;
}
unsigned nas_kthread_run(unsigned budget) {
    if(!budget || in_dispatch)return 0;
    in_dispatch=true;
    unsigned runs=0;
    while(runs<budget) {
        struct nas_kthread *chosen=0;
        for(unsigned offset=0;offset<NAS_KTHREAD_LIMIT;offset++) {
            const unsigned index=(next_slot+offset)%NAS_KTHREAD_LIMIT;
            if(workers[index].state==KTHREAD_READY) {
                chosen=&workers[index];
                next_slot=(index+1U)%NAS_KTHREAD_LIMIT;
                break;
            }
        }
        if(!chosen)break;
        active=chosen;
        chosen->state=KTHREAD_RUNNING;
        /* The caller's interrupt state and selected MM are unchanged:
         * this only swaps a kernel C stack, never a user return frame. */
        nas_arch_kernel_swap(&caller_context,&chosen->context);
        if(active!=chosen || chosen->state==KTHREAD_RUNNING)
            nas_fail("M95 kernel worker resumed without yielding");
        active=0;
        if(chosen->state==KTHREAD_DONE) {
            if(chosen->waiting.queue)
                nas_fail("M95 freeing queued worker");
            chosen->state=KTHREAD_FREE;
            nas_kernel_context_reset(&chosen->context);
            chosen->entry=0;
            chosen->argument=0;
        }
        ++runs;
    }
    in_dispatch=false;
    return runs;
}
unsigned nas_kthread_live(void) {
    unsigned n=0;
    for(unsigned i=0;i<NAS_KTHREAD_LIMIT;i++)
        if(workers[i].state!=KTHREAD_FREE)++n;
    return n;
}
