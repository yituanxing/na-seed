#include "wait_queue.h"
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

void nas_wait_queue_init(struct nas_wait_queue *q) {
    if(!q)return;
    *q=(struct nas_wait_queue){0};
}
void nas_wait_entry_reset(struct nas_wait_entry *e) {
    /* Only reset a detached entry; never discard live queue links. */
    if(!e || e->queue)return;
    *e=(struct nas_wait_entry){0};
}
bool nas_wait_queue_enqueue(struct nas_wait_queue *q,struct nas_wait_entry *e,
                            void (*notify)(struct nas_wait_entry *,void *),
                            void *opaque) {
    if(!q || !e || !notify || e->queue || q->count==UINT_MAX)return false;
    e->queue=q;
    e->notify=notify;
    e->opaque=opaque;
    e->armed_generation=q->generation;
    e->prev=q->last;
    e->next=0;
    if(q->last)q->last->next=e;
    else q->first=e;
    q->last=e;
    q->count++;
    return true;
}
bool nas_wait_queue_remove(struct nas_wait_entry *e) {
    if(!e || !e->queue)return false;
    struct nas_wait_queue *q=e->queue;
    if(e->prev)e->prev->next=e->next;
    else q->first=e->next;
    if(e->next)e->next->prev=e->prev;
    else q->last=e->prev;
    q->count--;
    e->queue=0;
    e->next=0;
    e->prev=0;
    e->notify=0;
    e->opaque=0;
    e->armed_generation=0;
    return true;
}
static bool match_all(const struct nas_wait_entry *e,void *arg) {
    (void)e;(void)arg;return true;
}
unsigned nas_wait_queue_wake_if(struct nas_wait_queue *q,unsigned maximum,
                                nas_wait_match_fn match,void *argument) {
    if(!q || !match)return 0;
    const unsigned limit=maximum?maximum:UINT_MAX;
    unsigned woken=0;
    if(++q->generation==0)++q->generation;
    const uint64_t pass=q->generation;
    while(woken<limit) {
        /* Callbacks can cancel/rearm any node: never hold a stale 'next'. */
        struct nas_wait_entry *e=q->first;
        while(e && e->armed_generation==pass)e=e->next;
        if(!e)break;
        e->armed_generation=pass;
        if(!match(e,argument))continue;
        void (*notify)(struct nas_wait_entry *,void *)=e->notify;
        void *opaque=e->opaque;
        (void)nas_wait_queue_remove(e);
        ++woken;
        notify(e,opaque);
    }
    return woken;
}
unsigned nas_wait_queue_wake(struct nas_wait_queue *q,unsigned maximum) {
    return nas_wait_queue_wake_if(q,maximum,match_all,0);
}
