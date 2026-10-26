#include "k_sync.h"
#include "kthread.h"
#include <limits.h>
#include <stdint.h>
/* In each primitive, inspecting the condition and enqueuing must be atomic
 * wrt its producer. NaSeed currently runs kernel C with local IRQs masked.
 * When preemptible kernel/SMP is added, replace this serialization by an
 * irqsave spinlock; do not add a second, per-device sleeper mechanism. */
void nas_ksem_init(struct nas_ksem *sem,unsigned initial) {
    if(!sem)return;
    *sem=(struct nas_ksem){0};
    sem->tokens=initial;
    nas_wait_queue_init(&sem->waiters);
}
bool nas_ksem_post(struct nas_ksem *sem) {
    if(!sem || sem->tokens==UINT_MAX)return false;
    ++sem->tokens;
    (void)nas_wait_queue_wake(&sem->waiters,1);
    return true;
}
bool nas_ksem_trywait(struct nas_ksem *sem) {
    if(!sem || !sem->tokens)return false;
    --sem->tokens;
    return true;
}
bool nas_ksem_wait(struct nas_ksem *sem) {
    if(!sem)return false;
    while(!nas_ksem_trywait(sem)) {
        if(!nas_kthread_park(&sem->waiters))return false;
    }
    return true;
}
void nas_kmailbox_init(struct nas_kmailbox *box) {
    if(!box)return;
    *box=(struct nas_kmailbox){0};
    nas_wait_queue_init(&box->readable);
    nas_wait_queue_init(&box->writable);
}
bool nas_kmailbox_try_send(struct nas_kmailbox *box,void *message) {
    if(!box || box->count>=NAS_KMAILBOX_CAP)return false;
    box->slots[box->tail]=message;
    box->tail=(box->tail+1U)%NAS_KMAILBOX_CAP;
    ++box->count;
    (void)nas_wait_queue_wake(&box->readable,1);
    return true;
}
bool nas_kmailbox_send(struct nas_kmailbox *box,void *message) {
    if(!box)return false;
    while(!nas_kmailbox_try_send(box,message)) {
        if(!nas_kthread_park(&box->writable))return false;
    }
    return true;
}
bool nas_kmailbox_try_recv(struct nas_kmailbox *box,void **message) {
    if(!box || !message || !box->count)return false;
    *message=box->slots[box->head];
    box->slots[box->head]=0;
    box->head=(box->head+1U)%NAS_KMAILBOX_CAP;
    --box->count;
    (void)nas_wait_queue_wake(&box->writable,1);
    return true;
}
bool nas_kmailbox_recv(struct nas_kmailbox *box,void **message) {
    if(!box || !message)return false;
    while(!nas_kmailbox_try_recv(box,message)) {
        if(!nas_kthread_park(&box->readable))return false;
    }
    return true;
}
