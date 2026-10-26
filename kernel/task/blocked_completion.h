#ifndef NASEED_BLOCKED_COMPLETION_H
#define NASEED_BLOCKED_COMPLETION_H
#include <stdint.h>
struct nas_task;
enum nas_blocked_completion {
    NAS_BLOCKED_COMPLETE=0,
    NAS_BLOCKED_RETRY,
    NAS_BLOCKED_TERMINATED
};
/* Invoked only by the owning resumed syscall C stack. The enclosing
 * ptrace syscall gate publishes the one actual exit event. */
enum nas_blocked_completion nas_blocked_complete(struct nas_task *,
                                                 uint64_t *result);
#endif
