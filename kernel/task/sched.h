#ifndef NASEED_TASK_SCHED_H
#define NASEED_TASK_SCHED_H
#include <stdint.h>
#include <stdbool.h>
#include "../syscall/ops.h"
uint64_t nas_task_schedule(uint64_t *frame);
uint64_t nas_task_block(uint64_t *frame, enum nas_syscall_op nr, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
bool nas_sched_sigpipe_default(uint64_t fd);
void nas_sched_child_exit(uint32_t code, uint32_t signal);
#endif
