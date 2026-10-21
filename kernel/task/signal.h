#ifndef NASEED_TASK_SIGNAL_H
#define NASEED_TASK_SIGNAL_H
#include <stdint.h>
uint64_t nas_signal_action(uint64_t signum, uint64_t new_action,
                           uint64_t old_action, uint64_t size);
uint64_t nas_signal_procmask(uint64_t how, uint64_t set,
                             uint64_t old_set, uint64_t size);
#endif
