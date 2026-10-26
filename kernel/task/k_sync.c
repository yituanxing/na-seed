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
enum nas_kwait_result nas_ksem_wait_until(struct nas_ksem *sem,
                                          uint64_t deadline_ns) {
    if(!sem)return NAS_KWAIT_INVALID;
    while(!nas_ksem_trywait(sem)) {
        enum nas_kwait_result r=nas_kthread_park_until(&sem->waiters,
                                                       deadline_ns);
        if(r!=NAS_KWAIT_READY)return r;
    }
    return NAS_KWAIT_READY;
}
bool nas_ksem_wait(struct nas_ksem *sem) {
    return nas_ksem_wait_until(sem,0)==NAS_KWAIT_READY;
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
enum nas_kwait_result nas_kmailbox_send_until(struct nas_kmailbox *box,
                                               void *message,uint64_t deadline_ns) {
    if(!box)return NAS_KWAIT_INVALID;
    while(!nas_kmailbox_try_send(box,message)) {
        enum nas_kwait_result r=nas_kthread_park_until(&box->writable,
                                                       deadline_ns);
        if(r!=NAS_KWAIT_READY)return r;
    }
    return NAS_KWAIT_READY;
}
bool nas_kmailbox_send(struct nas_kmailbox *box,void *message) {
    return nas_kmailbox_send_until(box,message,0)==NAS_KWAIT_READY;
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
enum nas_kwait_result nas_kmailbox_recv_until(struct nas_kmailbox *box,
                                               void **message,uint64_t deadline_ns) {
    if(!box || !message)return NAS_KWAIT_INVALID;
    while(!nas_kmailbox_try_recv(box,message)) {
        enum nas_kwait_result r=nas_kthread_park_until(&box->readable,
                                                       deadline_ns);
        if(r!=NAS_KWAIT_READY)return r;
    }
    return NAS_KWAIT_READY;
}
bool nas_kmailbox_recv(struct nas_kmailbox *box,void **message) {
    return nas_kmailbox_recv_until(box,message,0)==NAS_KWAIT_READY;
}
