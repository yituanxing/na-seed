#ifndef NASEED_TASK_SIGNAL_H
#define NASEED_TASK_SIGNAL_H
#include <stdint.h>
#define NAS_SIGKILL 9U
uint64_t nas_signal_action(uint64_t signum, uint64_t new_action,
                           uint64_t old_action, uint64_t size);
uint64_t nas_signal_procmask(uint64_t how, uint64_t set,
                             uint64_t old_set, uint64_t size);
/* Linux pid_t group selectors, nonmaskable SIGKILL and signal-zero probe. */
uint64_t nas_signal_kill(uint64_t raw_pid, uint64_t signal, uint64_t *frame);
#endif
