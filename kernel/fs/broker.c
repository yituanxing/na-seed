#include "broker.h"
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
#include "../errno.h"
#include "../mm/heap.h"
#include "../panic.h"
#include "../task/kthread.h"
#include "../task/wait_queue.h"
#include "../lib/string.h"

#if !defined(NAS_ENABLE_M179_FS_IRQ) || !defined(NAS_ENABLE_VIRTIO_BLK_ASYNC)
#error M180 requires the M179 IRQ-backed filesystem worker
#endif

enum request_state { FS_NEW, FS_QUEUED, FS_RUNNING, FS_DONE,
                     FS_ABANDON_QUEUED, FS_ABANDON_RUNNING };
struct nas_fs_broker_request {
    struct nas_fs_broker_request *next;
    nas_fs_broker_work_fn work;
    nas_fs_broker_release_fn release;
    nas_fs_broker_abandon_fn abandon;
    void *payload;
    uint64_t id;
    int64_t result;
    unsigned refs;
    enum request_state state;
    bool task_attached;
    bool claimed;
};

static struct nas_fs_broker_request *head,*tail;
static struct nas_wait_queue wake_queue;
static struct nas_fs_broker_stats stats;
static uint64_t next_id;
static unsigned queued;
static bool initialized,shutdown_requested,worker_terminated;
static bool serving;

static void ensure_initialized(void) {
    if(initialized)return;
    nas_wait_queue_init(&wake_queue);
    initialized=true;
}
static void request_put(struct nas_fs_broker_request *request) {
    if(!request || !request->refs)nas_fail("M180 broker request double release");
    if(--request->refs)return;
    if(request->state==FS_RUNNING || request->state==FS_QUEUED ||
       request->state==FS_ABANDON_QUEUED || request->state==FS_ABANDON_RUNNING)
        nas_fail("M180 broker released active filesystem request");
    if(request->release)request->release(request->payload);
    if(request->payload && !nas_heap_free(request->payload))
        nas_fail("M180 broker request payload heap corrupt");
    if(!nas_heap_free(request))nas_fail("M180 broker request heap corrupt");
}
struct nas_fs_broker_request *nas_fs_broker_request_new(
    size_t payload_bytes,nas_fs_broker_work_fn work,
    nas_fs_broker_release_fn release) {
    if(!work || payload_bytes>(size_t)(16U*1024U*1024U))return NULL;
    ensure_initialized();
    if(shutdown_requested || worker_terminated || next_id==UINT64_MAX)
        return NULL;
    struct nas_fs_broker_request *request=nas_heap_calloc(1,sizeof(*request));
    if(!request)return NULL;
    if(payload_bytes) {
        request->payload=nas_heap_calloc(1,payload_bytes);
        if(!request->payload) {
            (void)nas_heap_free(request);
            return NULL;
        }
    }
    request->work=work;
    request->release=release;
    request->id=++next_id;
    request->refs=1;
    request->task_attached=true;
    return request;
}
void *nas_fs_broker_request_payload(struct nas_fs_broker_request *request) {
    return request && request->task_attached ?
           request->payload : NULL;
}
bool nas_fs_broker_request_set_abandon(struct nas_fs_broker_request *request,
                                       nas_fs_broker_abandon_fn abandon) {
    if(!request || !request->task_attached || request->state!=FS_NEW ||
       request->abandon)return false;
    request->abandon=abandon;
    return true;
}
bool nas_fs_broker_request_claim(struct nas_fs_broker_request *request) {
    if(!request || !request->task_attached || request->state!=FS_DONE ||
       request->claimed)return false;
    request->claimed=true;
    return true;
}
int nas_fs_broker_request_submit(struct nas_fs_broker_request *request) {
    if(!request || !request->task_attached || request->refs!=1 ||
       request->state!=FS_NEW || !request->work)return -EINVAL;
    if(nas_kthread_in_worker())return -EDEADLK;
    if(shutdown_requested || worker_terminated)return -ESHUTDOWN;
    if(request->refs==UINT32_MAX)return -EOVERFLOW;
    /* Single CPU: Task kernel code cannot be preempted by another Task.
     * Published worker ownership and queue insertion form one transaction. */
    ++request->refs;
    request->state=FS_QUEUED;
    request->next=NULL;
    if(tail)tail->next=request;
    else head=request;
    tail=request;
    if(++queued>stats.peak_queued)stats.peak_queued=queued;
    ++stats.submitted;
    (void)nas_wait_queue_wake(&wake_queue,1);
    return 0;
}
bool nas_fs_broker_request_ready(const struct nas_fs_broker_request *request) {
    return request && request->state==FS_DONE;
}
int64_t nas_fs_broker_request_result(const struct nas_fs_broker_request *request) {
    if(!request || request->state!=FS_DONE)return -EAGAIN;
    return request->result;
}
void nas_fs_broker_request_detach(struct nas_fs_broker_request *request) {
    if(!request)return;
    if(!request->task_attached)nas_fail("M180 broker Task double detach");
    request->task_attached=false;
    ++stats.detached;
    if(request->state!=FS_DONE && request->state!=FS_NEW)
        ++stats.abandoned;
    /* Worker finished but the Task was killed before adopting an inode.
     * Requeue its VFS destruction: release() may run on a Task stack and
     * cannot safely perform ext4 callbacks that themselves block for IRQ. */
    if(request->state==FS_DONE && request->abandon && !request->claimed) {
        if(worker_terminated)nas_fail("M180 VFS orphan after worker shutdown");
        ++request->refs;
        request->state=FS_ABANDON_QUEUED;
        request->next=NULL;
        if(tail)tail->next=request;
        else head=request;
        tail=request;
        if(++queued>stats.peak_queued)stats.peak_queued=queued;
        (void)nas_wait_queue_wake(&wake_queue,1);
    }
    request_put(request);
}
bool nas_fs_broker_has_work(void) {
    return head!=NULL || (stats.started!=stats.completed);
}
void nas_fs_broker_request_shutdown(void) {
    ensure_initialized();
    shutdown_requested=true;
    (void)nas_wait_queue_wake(&wake_queue,1);
}
bool nas_fs_broker_shutdown_complete(void) {
    return worker_terminated;
}
void nas_fs_broker_get_stats(struct nas_fs_broker_stats *out) {
    if(out)*out=stats;
}
void nas_fs_broker_note_task_parked(void) { ++stats.task_parked; }
void nas_fs_broker_note_task_woken(void) { ++stats.task_woken; }
void nas_fs_broker_serve(void) {
    if(!nas_kthread_in_worker() || serving || worker_terminated)
        nas_fail("M180 broker entered outside owner worker");
    ensure_initialized();
    serving=true;
    for(;;) {
        struct nas_fs_broker_request *request=head;
        if(!request) {
            if(shutdown_requested)break;
            /* No Task/IRQ can run until the worker deliberately parks.
             * The waiter registration cannot race queue publication. */
            if(!nas_kthread_park(&wake_queue))
                nas_fail("M180 broker idle waiter lost ownership");
            continue;
        }
        head=request->next;
        if(!head)tail=NULL;
        if(!queued)nas_fail("M180 broker queue underflow");
        --queued;
        request->next=NULL;
        if((request->state!=FS_QUEUED &&
            request->state!=FS_ABANDON_QUEUED) || !request->refs)
            nas_fail("M180 broker dequeued invalid request");
        if(request->state==FS_ABANDON_QUEUED) {
            request->state=FS_ABANDON_RUNNING;
            if(!request->abandon || request->task_attached || request->claimed)
                nas_fail("M180 broker invalid abandoned cleanup");
            request->abandon(request->payload);
            request->state=FS_DONE;
            request_put(request);
            continue;
        }
        request->state=FS_RUNNING;
        ++stats.started;
        /* Callback may park on block IRQ while retaining its own C stack;
         * no other Task or worker may reenter this filesystem's VFS mount. */
        int64_t result=request->task_attached ?
            request->work(request->payload) : -ECANCELED;
        request->result=result;
        request->state=FS_DONE;
        ++stats.completed;
        if(!request->task_attached && request->abandon && !request->claimed)
            request->abandon(request->payload);
        request_put(request); /* worker ref, Task may already be gone */
    }
    serving=false;
    worker_terminated=true;
}
#endif
