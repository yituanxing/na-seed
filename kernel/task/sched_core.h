#ifndef NASEED_SCHED_CORE_H
#define NASEED_SCHED_CORE_H
#include <stdbool.h>
#include <stdint.h>
struct nas_task;
/* One fairness-preserving ring scan for timer IRQ and syscall dispatch:
 * READY users and awakened BLOCKED syscall continuations are peers.
 * Scan starts at the native_cursor and visits each slot once; a permanently
 * READY worker must never starve an awakened I/O waiter. */
struct nas_sched_scan {
    const struct nas_task *exclude;
    unsigned start_slot;
    unsigned visit_limit;
    unsigned offset;
};
void nas_sched_scan_init(struct nas_sched_scan *,const struct nas_task *exclude,
                          unsigned start_slot,unsigned visit_limit);
struct nas_task *nas_sched_scan_next(struct nas_sched_scan *,unsigned *slot);
/* Commit only after restoring the chosen Task CPU and finishing any blocked
 * syscall. User frame ownership is still with its architecture-specific
 * entry/return code, not with this scheduler policy. */
bool nas_sched_user_commit(struct nas_task *previous,
                           struct nas_task *next,bool preempt_previous);
/* One user Task readiness rule across syscall and timer IRQ scheduling.
 * Callers still own architecture trap-frame and MM/TLS/FPU transitions;
 * this deliberately does not synthesize kernel continuations for users. */
bool nas_sched_user_eligible(const struct nas_task *candidate,
                             const struct nas_task *running);
/* Domain fairness: bound native kernel work while user Tasks are runnable.
 * The actual C-stack swap remains in the kernel worker provider. */
bool nas_sched_allow_kernel_slice(bool user_ready,bool kernel_ready);
bool nas_sched_has_ready_users(void);
#endif
