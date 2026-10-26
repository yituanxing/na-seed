#ifndef NASEED_TASK_EXEC_H
#define NASEED_TASK_EXEC_H
#include <stdint.h>
uint64_t nas_task_execve(uint64_t pathname, uint64_t argv, uint64_t envp,
                          uint64_t *syscall_frame);
#endif
