#ifndef NASEED_TASK_PROCESS_H
#define NASEED_TASK_PROCESS_H
#include <stdint.h>
#include <stdbool.h>
#include "signal.h" /* canonical Linux signal numbers */
struct nas_task;
#define NAS_WAIT_NOHANG    1ULL
#define NAS_WAIT_UNTRACED  2ULL
#define NAS_WAIT_CONTINUED 8ULL
/* Honest read-only Linux rlimit64 snapshot; changes remain unsupported. */
uint64_t nas_process_prlimit(uint64_t pid,uint64_t resource,
                             uint64_t new_addr,uint64_t old_addr);
uint64_t nas_process_fork(uint64_t *syscall_frame);
/* Exactly fork-equivalent CLONE_CHILD_SETTID|CHILD_CLEARTID|SIGCHLD
 * process creation. Child-address-space TID is initialized before entry.
 * No shared-VM threads, CLONE_VFORK or alternate child stack are claimed. */
uint64_t nas_process_fork_child_tid(uint64_t *syscall_frame,
                                    uint64_t child_tid);
/* The Linux CLONE_VM|CLONE_VFORK|SIGCHLD subset of posix_spawn.
 * Shared root and suspended parent until child execve/exit. */
uint64_t nas_process_vfork(uint64_t *frame, uint64_t child_stack);
void nas_process_vfork_release(void);
uint64_t nas_process_set_robust_list(uint64_t head,uint64_t len);
uint64_t nas_process_get_robust_list(uint64_t pid,uint64_t head_addr,uint64_t len_addr);
/* Called with exiting task's shared VM installed before clearing its TID. */
void nas_process_robust_exit(struct nas_task *thread);
/* Commit-phase only: the caller has an image ready, old VM active, and
 * no other CPU may run a sibling before old page-table destruction.
 * Does not touch the thread-group leader's task slot: exec.c adopts it. */
void nas_process_exec_quiesce_siblings(void);
void nas_process_clear_tid_on_exit(void);
/* The caller's current VM must be identical to the target thread's VM. */
void nas_process_clear_tid_for_exit(struct nas_task *thread);
uint64_t nas_process_clone_thread(uint64_t *frame,uint64_t stack,
                                 uint64_t parent_tid,uint64_t child_tid,
                                 uint64_t tls);
uint64_t nas_process_futex(uint64_t *frame,uint64_t uaddr,uint64_t op,
                           uint64_t val,uint64_t timeout,uint64_t uaddr2,
                           uint64_t val3);
unsigned nas_process_futex_wake(uint64_t uaddr,unsigned n,uint32_t mask);
bool nas_process_waitable(const struct nas_task *child, uint64_t options);
uint64_t nas_process_wait4(uint64_t pid, uint64_t status, uint64_t options, uint64_t rusage);
#endif
