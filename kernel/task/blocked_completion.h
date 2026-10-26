#ifndef NASEED_BLOCKED_COMPLETION_H
#define NASEED_BLOCKED_COMPLETION_H
#include <stdint.h>
struct nas_task;
enum nas_blocked_completion {
    NAS_BLOCKED_COMPLETE=0,
    NAS_BLOCKED_RETRY,
    NAS_BLOCKED_TERMINATED,
    NAS_BLOCKED_PTRACE_STOP
};
/* Called with task's MM/TLS/FPU restored, from BOTH IRQ and syscall
 * scheduling. Legacy replay stays here until C continuations fully replace
 * it, never in the generic scheduler policy. Result is always the actual
 * Linux syscall result, not an invented success. */
enum nas_blocked_completion nas_blocked_complete(struct nas_task *,
                                                 uint64_t *result);
#endif
