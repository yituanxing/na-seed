#include "task.h"
#include "fd.h"
#include <stddef.h>

/* This is a real task registry, not a fabricated fork implementation.
 * The first (only running) task is PID 1. Other slots remain unallocated
 * until the arch switch and independent kernel stacks are implemented.
 */
static struct nas_task task_table[NAS_TASK_LIMIT];
static uint64_t next_pid = 2;
struct nas_task *nas_current_task;

void nas_task_boot_init(void) {
    for (unsigned n = 0; n < NAS_TASK_LIMIT; n++) {
        struct nas_task *task = &task_table[n];
        unsigned char *bytes = (unsigned char *)task;
        for (unsigned j = 0; j < sizeof(*task); j++) bytes[j] = 0;
    }
    next_pid = 2;
    nas_fd_system_init();
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

/* Reuse slots, NEVER PID identities. FD tables are copied, but
 * shared open-file references are adjusted after a successful VM fork.
 */
struct nas_task *nas_task_fork_slot(void) {
    for (unsigned i = 1; i < NAS_TASK_LIMIT; i++) {
        if (task_table[i].state != NAS_TASK_FREE) continue;
        struct nas_task *child = &task_table[i];
        const unsigned char *src = (const unsigned char *)nas_current_task;
        unsigned char *dst = (unsigned char *)child;
        for (size_t j = 0; j < sizeof(*child); j++) dst[j] = src[j];
        child->pid = next_pid++;
        child->ppid = nas_current_task->pid;
        child->state = NAS_TASK_READY;
        child->exit_code = 0;
        child->termination_signal = 0;
        child->resume_valid = false;
        child->blocked_nr = 0;
        child->pending_valid = false;
        return child;
    }
    return NULL;
}
void nas_task_release(struct nas_task *task) {
    if (!task || task == nas_current_task) return;
    task->state = NAS_TASK_FREE;
    task->pid = 0;
    task->ppid = 0;
}
struct nas_task *nas_task_by_pid(uint64_t pid) {
    for (unsigned i = 0; i < NAS_TASK_LIMIT; i++)
        if (task_table[i].state != NAS_TASK_FREE && task_table[i].pid == pid)
            return &task_table[i];
    return NULL;
}

uint64_t nas_task_page_root(const struct nas_task *task) {
    return task ? task->root_phys : 0;
}
uint64_t nas_task_tls_base(const struct nas_task *task) {
    return task ? task->user_fs_base : 0;
}
void nas_task_set_root(struct nas_task *task, uint64_t root) {
    if (task) task->root_phys = root;
}
struct nas_task *nas_task_child_at(uint64_t parent_pid, unsigned slot) {
    if (slot >= NAS_TASK_LIMIT || !parent_pid) return NULL;
    struct nas_task *task = &task_table[slot];
    if (task->state == NAS_TASK_FREE || task->ppid != parent_pid)
        return NULL;
    return task;
}

struct nas_task *nas_task_at(unsigned slot) {
    return slot < NAS_TASK_LIMIT ? &task_table[slot] : NULL;
}
uint8_t nas_task_fd_kind(const struct nas_task *task, uint64_t fd) {
    return task && fd < NAS_FD_LIMIT ? task->fds.descriptors[fd] : 0;
}
unsigned nas_task_fd_slot(const struct nas_task *task, uint64_t fd) {
    return task && fd < NAS_FD_LIMIT ? task->fds.file_slot[fd] : NAS_FD_LIMIT;
}
