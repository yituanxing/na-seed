#ifndef NASEED_TASK_SIGNAL_H
#define NASEED_TASK_SIGNAL_H
#include <stdint.h>
#include <stdbool.h>
#define NAS_SIGINT 2U
#define NAS_SIGPIPE 13U
#define NAS_SIGTERM 15U
#define NAS_SIGQUIT 3U
#define NAS_SIGTSTP 20U
#define NAS_SIGKILL 9U
#define NAS_SIGCONT 18U
#define NAS_SIGSTOP 19U
uint64_t nas_signal_action(uint64_t signum, uint64_t new_action,
                           uint64_t old_action, uint64_t size);
uint64_t nas_signal_procmask(uint64_t how, uint64_t set,
                             uint64_t old_set, uint64_t size,uint64_t *frame);
uint64_t nas_signal_pending(uint64_t destination,uint64_t size);
/* Linux pid_t group selectors, nonmaskable SIGKILL and signal-zero probe. */
uint64_t nas_signal_kill(uint64_t raw_pid, uint64_t signal, uint64_t *frame);
bool nas_signal_tty_event(uint64_t sid,uint64_t pgid,unsigned signum);
#endif
