#ifndef NASEED_TASK_SCHED_H
#define NASEED_TASK_SCHED_H
#include <stdint.h>
#include <stdbool.h>
#include "../syscall/ops.h"
uint64_t nas_task_schedule(uint64_t *frame);
/* Called only from a fully saved user-mode timer IRQ. Caller owns all
 * register/FPU capture and final architecture return-frame reconstruction. */
/* Native IRQ return always resumes its own parked Task C continuation.
 * Cross-Task selection occurs on the scheduler stack, never by copying
 * another Task's user image into an IRQ frame. */
void nas_sched_preempt_select(void);
struct nas_task;
uint64_t nas_task_block(uint64_t *frame, enum nas_syscall_op nr,
                        uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                        uint64_t (*finish)(struct nas_task *));
bool nas_sched_sigpipe_default(bool pipe_like);
void nas_sched_child_exit(uint32_t code, uint32_t signal);
/* Linux exit_group terminates EVERY member, including blocked pthreads. */
void nas_sched_thread_group_exit(uint32_t code, uint32_t signal);
#endif
