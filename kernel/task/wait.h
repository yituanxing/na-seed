#ifndef NASEED_TASK_WAIT_H
#define NASEED_TASK_WAIT_H
#include <stdbool.h>
struct nas_task;
bool nas_task_wait_ready(const struct nas_task *task);
void nas_task_wait_reset(struct nas_task *task);
#endif
