#ifndef NASEED_TASK_SIGNAL_H
#define NASEED_TASK_SIGNAL_H
#include <stdint.h>
#include <stdbool.h>
struct nas_task;
#define NAS_SIGINT 2U
#define NAS_SIGTRAP 5U
#define NAS_SIGSEGV 11U
#define NAS_SIGUSR1 10U
#define NAS_SIGUSR2 12U
#define NAS_SIGPIPE 13U
#define NAS_SIGCHLD 17U
#define NAS_SIGTERM 15U
#define NAS_SIGQUIT 3U
#define NAS_SIGTSTP 20U
#define NAS_SIGTTIN 21U
#define NAS_SIGTTOU 22U
#define NAS_SIGKILL 9U
#define NAS_SIGCONT 18U
#define NAS_SIGSTOP 19U
#define NAS_SA_ONSTACK UINT64_C(0x08000000)
/* Linux stack_t ABI is the same 24-byte LP64 struct on all three ports. */
#define NAS_SS_ONSTACK 1U
#define NAS_SS_DISABLE 2U
uint64_t nas_signal_action(uint64_t signum, uint64_t new_action,
                           uint64_t old_action, uint64_t size);
uint64_t nas_signal_sigaltstack(uint64_t new_stack, uint64_t old_stack);
uint32_t nas_signal_altstack_flags(const struct nas_task *task,
                                   uint64_t current_user_sp);
uint64_t nas_signal_handler_stack_top(const struct nas_task *task,
                                      uint64_t current_user_sp,
                                      uint64_t action_flags);
uint64_t nas_signal_procmask(uint64_t how, uint64_t set,
                             uint64_t old_set, uint64_t size,uint64_t *frame);
/* Always run with the chosen Task's userspace MMU active and a complete
 * user-return trap image; never deliver by switching to a foreign task. */
void nas_signal_user_return(uint64_t *frame);
uint64_t nas_signal_sigreturn(uint64_t *frame);
/* Install native RX restorer into each new user address space, before exec. */
bool nas_signal_install_trampoline(void);
bool nas_signal_user_pending(const struct nas_task *task);
void nas_signal_ppoll_restore(struct nas_task *task,bool interrupted);
/* Emit SIGPIPE for caught or masked broken-pipe writes; default action is
 * handled by the scheduler before returning to userspace. */
void nas_signal_broken_pipe(bool pipe_like);
/* A reaped child does not vanish when SIGCHLD is ignored: deliver a
 * caught/blocked change notification separately from wait4 zombie state. */
void nas_signal_child_exit(const struct nas_task *child);
/* A ptrace STOP, unlike a normal job STOP, notifies the AUTHORIZED tracer.
 * The real parent remains unchanged; a caught SIGCHLD must interrupt the
 * tracer's blocking poll/epoll so a debugger can run waitpid/__WALL. */
void nas_signal_ptrace_stop(const struct nas_task *tracee);
/* Called by the selected ISA's frame provider after saving the old mask.
 * Correct SA_NODEFER/RESETHAND semantics are owned by the common core. */
void nas_signal_handler_enter(struct nas_task *task,unsigned signum,
                              const uint64_t action[4]);
uint64_t nas_signal_pending(uint64_t destination,uint64_t size);
/* Linux pid_t group selectors, nonmaskable SIGKILL and signal-zero probe. */
uint64_t nas_signal_kill(uint64_t raw_pid, uint64_t signal, uint64_t *frame);
uint64_t nas_signal_tkill(uint64_t raw_tid,uint64_t signum,uint64_t *frame);
uint64_t nas_signal_tgkill(uint64_t raw_tgid,uint64_t raw_tid,
                           uint64_t signum,uint64_t *frame);
bool nas_signal_tty_event(uint64_t sid,uint64_t pgid,unsigned signum);
/* A background job's TTY read/write/ioctl may STOP the whole process
 * group; on SIGCONT the current syscall completes EINTR (caller retries).
 * If handled is false, continue the underlying fd operation normally. */
uint64_t nas_signal_background_job(unsigned signum,uint64_t *frame,
                                    bool *handled);
#endif
