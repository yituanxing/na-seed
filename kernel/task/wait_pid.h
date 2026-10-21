#ifndef NASEED_WAIT_PID_H
#define NASEED_WAIT_PID_H
#include <stdint.h>
#include <stdbool.h>
/* Linux wait4 consumes a SIGNED 32-bit pid_t on all LP64 targets.
 * The 64-bit register can zero-extend or sign-extend the same pid_t;
 * higher bits never define process/group selection.
 */
static inline int32_t nas_wait_pid_arg(uint64_t raw) {
    return (int32_t)(uint32_t)raw;
}
static inline bool nas_wait_pid_matches(uint64_t raw, uint64_t child_pid,
                                        uint64_t parent_pgid,
                                        uint64_t child_pgid) {
    int32_t desired=nas_wait_pid_arg(raw);
    if (desired == -1) return true;
    if (desired > 0) return child_pid == (uint64_t)desired;
    if (desired == 0) return child_pgid == parent_pgid;
    /* Signed 64-bit negation handles INT32_MIN without UB. */
    return child_pgid == (uint64_t)(-(int64_t)desired);
}
#endif
