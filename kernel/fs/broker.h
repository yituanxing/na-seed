#ifndef NASEED_FS_BROKER_H
#define NASEED_FS_BROKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* M180: syscall tasks and the IRQ-backed filesystem do NOT share a C stack.
 * A Task owns the request until it has consumed its result or detached; the
 * dedicated kthread owns an independent reference from submit to completion.
 * The payload is kernel memory, never a user address or borrowed Task stack.
 * A work callback must invoke ONLY kernel-buffer VFS operations and must not
 * access nas_current_task, a Task's userspace mappings, or syscall ABI data.
 * A release callback may reclaim kernel allocations only; provider VFS
 * resources that must be released on the worker should be released by work.
 */
struct nas_fs_broker_request;
typedef int64_t (*nas_fs_broker_work_fn)(void *kernel_payload);
typedef void (*nas_fs_broker_release_fn)(void *kernel_payload);
typedef void (*nas_fs_broker_abandon_fn)(void *kernel_payload);

struct nas_fs_broker_stats {
    uint64_t submitted, started, completed, detached, abandoned;
    uint64_t peak_queued, task_parked, task_woken;
};

#ifdef NAS_ENABLE_M180_FS_USER_IRQ
struct nas_fs_broker_request *nas_fs_broker_request_new(
    size_t payload_bytes, nas_fs_broker_work_fn work,
    nas_fs_broker_release_fn release);
void *nas_fs_broker_request_payload(struct nas_fs_broker_request *request);
/* A work item returning an owning VFS Path/inode may install an optional
 * worker-only abandonment hook. The Task calls claim() once those refs are
 * transferred into a published File; otherwise any Task cancellation/exit
 * schedules their release back on the same filesystem worker. */
bool nas_fs_broker_request_set_abandon(struct nas_fs_broker_request *request,
                                       nas_fs_broker_abandon_fn abandon);
bool nas_fs_broker_request_claim(struct nas_fs_broker_request *request);
/* Return 0 only after an independent worker reference owns the queue entry.
 * On negative errno the caller still owns the newly allocated request. */
int nas_fs_broker_request_submit(struct nas_fs_broker_request *request);
bool nas_fs_broker_request_ready(const struct nas_fs_broker_request *request);
int64_t nas_fs_broker_request_result(const struct nas_fs_broker_request *request);
/* Releases the Task's reference. Safe on a QUEUED or RUNNING request: the
 * worker still owns it and may finish without dereferencing a dead Task. */
void nas_fs_broker_request_detach(struct nas_fs_broker_request *request);
bool nas_fs_broker_has_work(void);
void nas_fs_broker_request_shutdown(void);
bool nas_fs_broker_shutdown_complete(void);
void nas_fs_broker_get_stats(struct nas_fs_broker_stats *out);
/* Call from the actual Task syscall park/finish sites, not from queue
 * submission or a speculative ready predicate. */
void nas_fs_broker_note_task_parked(void);
void nas_fs_broker_note_task_woken(void);
/* Only the owner kthread should call serve(). It parks on an independent
 * wait queue until new work arrives, and exits after drain+shutdown. */
void nas_fs_broker_serve(void);
#else
/* Wait/scheduler code includes this unconditionally. Historical kernels
 * without M180 do not need another linked object or a fake worker. */
static inline bool nas_fs_broker_request_ready(
    const struct nas_fs_broker_request *request) {
    (void)request; return false;
}
static inline void nas_fs_broker_request_detach(
    struct nas_fs_broker_request *request) { (void)request; }
#endif
#endif
