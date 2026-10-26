#ifndef NASEED_K_SYNC_H
#define NASEED_K_SYNC_H
#include <stdbool.h>
#include <stdint.h>
#include "wait_queue.h"

/* Single CPU, IRQ-masked synchronous primitives for genuine kernel C
 * continuations. No dynamic allocation, no user addresses, no syscall
 * numbering. Producer operations never context-switch and can be called
 * from a serialized IRQ handler. Sleeping requires a live kernel worker.
 * No timeouts, close or priority inheritance yet (NOT a full lwIP port). */
struct nas_ksem {
    unsigned tokens;
    struct nas_wait_queue waiters;
};
void nas_ksem_init(struct nas_ksem *,unsigned initial);
bool nas_ksem_post(struct nas_ksem *);
bool nas_ksem_trywait(struct nas_ksem *);
bool nas_ksem_wait(struct nas_ksem *);

#define NAS_KMAILBOX_CAP 8U
struct nas_kmailbox {
    void *slots[NAS_KMAILBOX_CAP];
    unsigned head,tail,count;
    struct nas_wait_queue readable,writable;
};
void nas_kmailbox_init(struct nas_kmailbox *);
bool nas_kmailbox_try_send(struct nas_kmailbox *,void *message);
bool nas_kmailbox_send(struct nas_kmailbox *,void *message);
bool nas_kmailbox_try_recv(struct nas_kmailbox *,void **message);
bool nas_kmailbox_recv(struct nas_kmailbox *,void **message);
#endif
