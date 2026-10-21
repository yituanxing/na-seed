#ifndef NASEED_TASK_PROCESS_H
#define NASEED_TASK_PROCESS_H
#include <stdint.h>
#define NAS_SIGPIPE 13 /* Linux SIGPIPE */
uint64_t nas_process_fork(uint64_t *syscall_frame);
uint64_t nas_process_wait4(uint64_t pid, uint64_t status, uint64_t options, uint64_t rusage);
#endif
