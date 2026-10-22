#ifndef NASEED_WAIT_PID_H
#define NASEED_WAIT_PID_H
#include <stdint.h>
#include <stdbool.h>
/* Linux wait4 pid_t is a SIGNED 32-bit scalar on all three LP64 ABIs.
 * Different libc/ISA code can zero-extend or sign-extend -1 to a 64-bit
 * syscall register; only the low 32 bits carry a Linux pid_t. */
static inline int32_t nas_wait_pid_arg(uint64_t raw) {
    return (int32_t)(uint32_t)raw;
}
static inline bool nas_wait_pid_supported(uint64_t raw) {
    int32_t pid=nas_wait_pid_arg(raw);
    return pid==-1 || pid>0;
}
static inline bool nas_wait_pid_matches(uint64_t raw,uint64_t child_pid) {
    int32_t pid=nas_wait_pid_arg(raw);
    return pid==-1 || (pid>0 && child_pid==(uint64_t)pid);
}
#endif
