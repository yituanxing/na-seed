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
    unsigned start_slot;
    unsigned offset;
};
/* The native scheduler always scans exactly one full ring. There are no
 * arbitrary truncation or excluded-task parameters: only the Task state
 * machine decides whether a candidate can run. */
void nas_sched_scan_init(struct nas_sched_scan *,unsigned start_slot);
struct nas_task *nas_sched_scan_next(struct nas_sched_scan *,unsigned *slot);
/* One user Task readiness rule across native syscall and timer IRQ paths.
 * The dispatcher, not this policy layer, commits Task state and CPU/MM. */
bool nas_sched_user_eligible(const struct nas_task *candidate);
/* Domain fairness: bound native kernel work while user Tasks are runnable.
 * The actual C-stack swap remains in the kernel worker provider. */
bool nas_sched_allow_kernel_slice(bool user_ready,bool kernel_ready);
bool nas_sched_has_ready_users(void);
#endif
