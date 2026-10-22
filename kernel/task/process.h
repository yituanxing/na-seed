#ifndef NASEED_TASK_PROCESS_H
#define NASEED_TASK_PROCESS_H
#include <stdint.h>
#include <stdbool.h>
#include "signal.h" /* canonical Linux signal numbers */
struct nas_task;
#define NAS_WAIT_NOHANG    1ULL
#define NAS_WAIT_UNTRACED  2ULL
#define NAS_WAIT_CONTINUED 8ULL
uint64_t nas_process_fork(uint64_t *syscall_frame);
/* Exactly fork-equivalent CLONE_CHILD_SETTID|CHILD_CLEARTID|SIGCHLD
 * process creation. Child-address-space TID is initialized before entry.
 * No shared-VM threads, CLONE_VFORK or alternate child stack are claimed. */
uint64_t nas_process_fork_child_tid(uint64_t *syscall_frame,
                                    uint64_t child_tid);
void nas_process_clear_tid_on_exit(void);
bool nas_process_waitable(const struct nas_task *child, uint64_t options);
uint64_t nas_process_wait4(uint64_t pid, uint64_t status, uint64_t options, uint64_t rusage);
#endif
