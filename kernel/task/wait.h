#ifndef NASEED_TASK_WAIT_H
#define NASEED_TASK_WAIT_H
#include <stdbool.h>
#include "../task.h"
bool nas_task_wait_ready(const struct nas_task *task);
/* Pure by-value reset: standalone task.c fixtures need no device/provider link.
 * The scheduler readiness provider remains a separate shared C unit. */
static inline void nas_task_wait_reset(struct nas_task *task) {
    if (task) task->wait=(struct nas_task_wait){0};
}
#endif
