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
unsigned nas_wait_queue_wake(struct nas_wait_queue *q,unsigned max_count) {
    if(!q)return 0;
    unsigned budget=q->count;
    unsigned maximum=max_count ? max_count : UINT_MAX;
    unsigned woken=0;
    /* Increment once per wake batch. An entry enqueued by a callback carries
     * this generation, and may not be reawakened in the same iteration. */
    if(++q->generation==0)++q->generation;
    while(q->first && budget && woken<maximum) {
        struct nas_wait_entry *e=q->first;
        if(e->armed_generation==q->generation)break;
        void (*notify)(struct nas_wait_entry *,void *)=e->notify;
        void *opaque=e->opaque;
        nas_wait_queue_remove(e); /* unlink BEFORE invoking callback */
        --budget;
        ++woken;
        notify(e,opaque);
    }
    return woken;
}
