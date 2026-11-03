/* Host-side M180 ownership regression. Compile this file WITH the real
 * kernel/fs/broker.c (never a test-local copy of its queue/state machine):
 *
 * gcc -std=c11 -O2 -Wall -Wextra -Werror \
 *   -DNAS_ENABLE_M180_FS_USER_IRQ=1 -DNAS_ENABLE_M179_FS_IRQ=1 \
 *   -DNAS_ENABLE_VIRTIO_BLK_ASYNC=1 \
 *   scripts/test-m180-fs-broker.c kernel/fs/broker.c \
 *   -o /tmp/test-m180-fs-broker && /tmp/test-m180-fs-broker
 *
 * The fake scheduler is deliberately single-CPU: a producer runs before
 * or while the worker is parked; no code borrows a detached Task payload.
 * Heap/free, wakeups and each work/abandon callback are independently
 * observed so an abandoned ordinary request cannot masquerade as an
 * explicitly independent fire-and-forget write/flush operation.
 */
#include "../kernel/fs/broker.h"
#include "../kernel/task/wait_queue.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIRE_INITIAL=1, ABORTED=2, CLAIMED=3, FIRE_DRAIN=4 };
struct test_payload { unsigned tag; uint64_t cookie; };

static unsigned allocations,frees,wakes,parks,park_injections;
static unsigned works[5],releases[5],abandons[5];
static bool current_is_worker,waiting;
static struct nas_wait_queue *broker_queue;
static struct nas_fs_broker_request *claimed_request;

void nas_print(const char *message) { fputs(message,stderr); }
__attribute__((noreturn)) void nas_arch_halt(void) { abort(); }

void *nas_heap_calloc(size_t count,size_t bytes) {
    void *result=calloc(count,bytes);
    if(result)allocations++;
    return result;
}
bool nas_heap_free(void *pointer) {
    assert(pointer);
    free(pointer);
    frees++;
    return true;
}
void nas_wait_queue_init(struct nas_wait_queue *queue) {
    assert(queue && !broker_queue);
    *queue=(struct nas_wait_queue){0};
    broker_queue=queue;
}
unsigned nas_wait_queue_wake(struct nas_wait_queue *queue,unsigned max_count) {
    assert(queue==broker_queue && max_count==1);
    wakes++;
    if(!waiting)return 0;
    waiting=false;
    return 1;
}
bool nas_kthread_in_worker(void) { return current_is_worker; }

static int64_t genuine_work(void *payload) {
    struct test_payload *value=payload;
    assert(current_is_worker && value && value->cookie==UINT64_C(0xcafe1234));
    assert(value->tag>=1 && value->tag<=FIRE_DRAIN);
    works[value->tag]++;
    return (int64_t)(value->tag*100U+7U);
}
static void released(void *payload) {
    struct test_payload *value=payload;
    assert(value && value->cookie==UINT64_C(0xcafe1234));
    assert(value->tag>=1 && value->tag<=FIRE_DRAIN);
    /* Claimed File ownership is consumed by its Task. The generic payload
     * release is safe there; actual VFS orphan cleanup is separate and
     * required to stay on the worker through abandon(). */
    assert(current_is_worker==(value->tag!=CLAIMED));
    releases[value->tag]++;
    value->cookie=0; /* Detect use-after-release in callbacks. */
}
static void abandoned(void *payload) {
    struct test_payload *value=payload;
    assert(current_is_worker && value && value->cookie==UINT64_C(0xcafe1234));
    assert(value->tag==ABORTED || value->tag==CLAIMED);
    abandons[value->tag]++;
}
static struct nas_fs_broker_request *new_request(unsigned tag) {
    struct nas_fs_broker_request *request=nas_fs_broker_request_new(
        sizeof(struct test_payload),genuine_work,released);
    assert(request);
    struct test_payload *value=nas_fs_broker_request_payload(request);
    assert(value);
    *value=(struct test_payload){.tag=tag,.cookie=UINT64_C(0xcafe1234)};
    assert(!nas_fs_broker_request_ready(request));
    assert(nas_fs_broker_request_result(request)==-EAGAIN);
    return request;
}
static void independently_submit_then_detach(unsigned tag) {
    assert(!current_is_worker);
    struct nas_fs_broker_request *request=new_request(tag);
    assert(nas_fs_broker_request_make_independent(request));
    assert(!nas_fs_broker_request_make_independent(request));
    assert(nas_fs_broker_request_submit(request)==0);
    assert(nas_fs_broker_request_submit(request)==-EINVAL);
    /* Nobody may dereference request after this detach; the worker now
     * owns the sole live reference, and MUST still execute its callback. */
    nas_fs_broker_request_detach(request);
}

/* broker.c calls this only after its real queue has drained. Pretend the
 * independent worker parked, then execute one external Task turn, which
 * consumes CLAIMED's result and submits another independent request.
 * Shutdown is deliberately requested WITH a request still queued: serve()
 * must drain that request before reporting worker_terminated. */
bool nas_kthread_park(struct nas_wait_queue *queue) {
    assert(current_is_worker && queue==broker_queue && !waiting);
    assert(!nas_fs_broker_has_work() && park_injections==0);
    parks++;
    waiting=true;
    current_is_worker=false;

    assert(nas_fs_broker_request_ready(claimed_request));
    assert(nas_fs_broker_request_result(claimed_request)==307);
    assert(nas_fs_broker_request_claim(claimed_request));
    assert(!nas_fs_broker_request_claim(claimed_request));
    nas_fs_broker_note_task_woken();
    nas_fs_broker_request_detach(claimed_request);
    claimed_request=NULL;
    independently_submit_then_detach(FIRE_DRAIN);
    assert(!waiting); /* The queued work really woke the parked worker. */
    nas_fs_broker_request_shutdown();
    assert(nas_fs_broker_has_work());
    assert(!nas_fs_broker_shutdown_complete());
    park_injections++;
    current_is_worker=true;
    return true;
}

int main(void) {
    assert(!nas_kthread_in_worker());
    assert(!nas_fs_broker_has_work());
    assert(!nas_fs_broker_shutdown_complete());

    independently_submit_then_detach(FIRE_INITIAL);

    /* A normal Task syscall detached BEFORE worker acceptance must not
     * create or modify the filesystem even if the queued pointer remains.
     * Its worker-side abandon callback still reclaims any owned result. */
    struct nas_fs_broker_request *aborted=new_request(ABORTED);
    assert(nas_fs_broker_request_set_abandon(aborted,abandoned));
    assert(!nas_fs_broker_request_set_abandon(aborted,abandoned));
    assert(!nas_fs_broker_request_make_independent(aborted));
    assert(nas_fs_broker_request_submit(aborted)==0);
    nas_fs_broker_request_detach(aborted);
    /* A task still waiting for a return value remains attached until the
     * worker completes. It claims the result before dropping its ref. */
    claimed_request=new_request(CLAIMED);
    assert(nas_fs_broker_request_set_abandon(claimed_request,abandoned));
    assert(!nas_fs_broker_request_claim(claimed_request));
    assert(nas_fs_broker_request_submit(claimed_request)==0);
    nas_fs_broker_note_task_parked();

    struct nas_fs_broker_stats before={0};
    nas_fs_broker_get_stats(&before);
    assert(before.submitted==3 && before.started==0 &&
           before.completed==0 && before.detached==2 &&
           before.abandoned==1 && before.peak_queued==3 &&
           before.task_parked==1 && before.task_woken==0);
    assert(nas_fs_broker_has_work());

    current_is_worker=true;
    nas_fs_broker_serve(); /* EXACT linked production state machine. */
    current_is_worker=false;

    assert(works[FIRE_INITIAL]==1 && works[ABORTED]==0 &&
           works[CLAIMED]==1 && works[FIRE_DRAIN]==1);
    assert(abandons[ABORTED]==1 && abandons[CLAIMED]==0);
    for(unsigned tag=FIRE_INITIAL;tag<=FIRE_DRAIN;tag++)
        assert(releases[tag]==1);
    assert(parks==1 && park_injections==1 && wakes>=5);
    assert(nas_fs_broker_shutdown_complete() && !nas_fs_broker_has_work());
    assert(nas_fs_broker_request_new(1,genuine_work,released)==NULL);
    struct nas_fs_broker_stats after={0};
    nas_fs_broker_get_stats(&after);
    assert(after.submitted==4 && after.started==4 &&
           after.completed==4 && after.detached==4 &&
           after.abandoned==1 && after.peak_queued==3 &&
           after.task_parked==1 && after.task_woken==1);
    assert(allocations==frees); /* No escaped detached request/payload. */
    puts("[M180 HOST BROKER LIFETIME PASS] real broker.c: independent "
         "submit/detach executes; canceled Task callback skipped; "
         "worker-only abandon/claim; parked wake; shutdown drains queued "
         "operations with no heap leaks");
    return 0;
}
