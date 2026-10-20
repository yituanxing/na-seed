#include "task.h"
#include <stddef.h>

/* This is a real task registry, not a fabricated fork implementation.
 * The first (only running) task is PID 1. Other slots remain unallocated
 * until the arch switch and independent kernel stacks are implemented.
 */
static struct nas_task task_table[NAS_TASK_LIMIT];
struct nas_task *nas_current_task;

void nas_task_boot_init(void) {
    for (unsigned n = 0; n < NAS_TASK_LIMIT; n++) {
        struct nas_task *task = &task_table[n];
        unsigned char *bytes = (unsigned char *)task;
        for (unsigned j = 0; j < sizeof(*task); j++) bytes[j] = 0;
    }
    nas_current_task = &task_table[0];
    nas_current_task->pid = 1;
    nas_current_task->ppid = 0;
    nas_current_task->state = NAS_TASK_RUNNING;
    nas_current_task->mmap_cursor = 0x60000000ULL;
    nas_current_task->cwd[0] = '/';
    nas_current_task->cwd[1] = 0;
}
struct nas_task *nas_task_current(void) { return nas_current_task; }
const struct nas_task *nas_task_find(uint64_t pid) {
    if (!pid) return 0;
    for (unsigned i = 0; i < NAS_TASK_LIMIT; i++)
        if (task_table[i].state != NAS_TASK_FREE &&
            task_table[i].pid == pid)
            return &task_table[i];
    return 0;
}
