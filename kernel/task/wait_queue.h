#ifndef NASEED_WAIT_QUEUE_H
#define NASEED_WAIT_QUEUE_H
#include <stdbool.h>
#include <stdint.h>

/* Intrusive, allocation-free single-CPU kernel wait queue.  An entry belongs
 * to its waiter, not to a syscall number, fd, device or VM.  Callers MUST
 * serialize condition inspection, enqueue and producer wake with local IRQs
 * masked (or an equivalent lock) to avoid lost wakeups.  Wake callbacks may
 * mark a task runnable but MUST NOT context-switch or block in IRQ context.
 * Queues do not own waiter memory: cancel before task/stack destruction. */
struct nas_wait_queue;
struct nas_wait_entry {
    struct nas_wait_entry *prev, *next;
    struct nas_wait_queue *queue;
    void (*notify)(struct nas_wait_entry *, void *);
    void *opaque;
    uint64_t armed_generation;
};
struct nas_wait_queue {
    struct nas_wait_entry *first, *last;
    unsigned count;
    uint64_t generation;
};
void nas_wait_queue_init(struct nas_wait_queue *);
void nas_wait_entry_reset(struct nas_wait_entry *);
bool nas_wait_queue_enqueue(struct nas_wait_queue *, struct nas_wait_entry *,
                            void (*notify)(struct nas_wait_entry *, void *),
                            void *opaque);
bool nas_wait_queue_remove(struct nas_wait_entry *);
/* max_count=0 means all waiters present at wake entry. Callbacks may enqueue
 * new waiters; those registrations are NOT notified by the same wake pass. */
unsigned nas_wait_queue_wake(struct nas_wait_queue *, unsigned max_count);
typedef bool (*nas_wait_match_fn)(const struct nas_wait_entry *,void *);
unsigned nas_wait_queue_wake_if(struct nas_wait_queue *,unsigned max_count,
                                nas_wait_match_fn match,void *argument);
#endif
