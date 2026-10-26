#ifndef NASEED_RUN_STATE_H
#define NASEED_RUN_STATE_H
#include <stdbool.h>
struct nas_task;
/* Single-CPU producer-side event wake. This is intentionally distinct
 * from scheduling: IRQ/FS code only marks runnable, never swaps contexts. */
bool nas_task_wake_ready(struct nas_task *task);
#endif
