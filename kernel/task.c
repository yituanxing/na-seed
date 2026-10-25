#include "task.h"
#include "fs/mode.h"
#include "arch/abi.h"
#include "fd.h"
#include "task/wait.h"
#include "lib/string.h"
#include <stddef.h>

/* Task-owned entry stacks. Cooperative syscall switching still unwinds
 * every C frame; this is NOT yet a resumable kernel continuation. */
static struct nas_task task_table[NAS_TASK_LIMIT];
static _Alignas(16) uint8_t task_kernel_stacks[NAS_TASK_LIMIT][NAS_KERNEL_STACK_BYTES];
#define NAS_KSTACK_SENTINEL UINT64_C(0x6e61736565646b73)
static uint64_t stack_top(unsigned slot) {
    return (uint64_t)(uintptr_t)&task_kernel_stacks[slot][NAS_KERNEL_STACK_BYTES];
}
static void stack_reset(unsigned slot) {
    *(uint64_t *)(void *)task_kernel_stacks[slot] = NAS_KSTACK_SENTINEL;
}
bool nas_task_kernel_stack_ok(const struct nas_task *task) {
    if (!task) return false;
    uintptr_t base=(uintptr_t)&task_table[0];
    uintptr_t end=(uintptr_t)&task_table[NAS_TASK_LIMIT];
    uintptr_t ptr=(uintptr_t)task;
    if (ptr < base || ptr >= end || (ptr-base)%sizeof(task_table[0])) return false;
    unsigned slot=(unsigned)((ptr-base)/sizeof(task_table[0]));
    return task->kernel_stack_top == stack_top(slot) &&
        *(const uint64_t *)(const void *)task_kernel_stacks[slot] == NAS_KSTACK_SENTINEL;
}
static uint64_t next_pid = 2;
void nas_task_set_commandline(struct nas_task *task,
                              const char *const *argv,unsigned argc) {
    if(!task)return;
    unsigned used=0;
    if(!argv || !argc){task->commandline_bytes=0;return;}
    for(unsigned i=0;i<argc;i++) {
        const char *arg=argv[i];
        if(!arg){task->commandline_bytes=0;return;}
        unsigned n=0;while(arg[n] && n<sizeof(task->commandline))n++;
        if(n==sizeof(task->commandline) ||
           n+1U>sizeof(task->commandline)-used) {
            task->commandline_bytes=0;
            return;
        }
        memcpy(task->commandline+used,arg,n+1U);
        used+=n+1U;
    }
    task->commandline_bytes=(uint16_t)used;
}
struct nas_task *nas_current_task;

void nas_task_boot_init(void) {
    for (unsigned n = 0; n < NAS_TASK_LIMIT; n++) {
        struct nas_task *task = &task_table[n];
        memset(task, 0, sizeof(*task));
        stack_reset(n);
        task->kernel_stack_top = stack_top(n);
    }
    next_pid = 2;
    nas_fd_system_init();
    nas_current_task = &task_table[0];
    nas_current_task->pid = 1;
    nas_current_task->tgid=1;
    nas_current_task->vm_owner=nas_current_task;
    nas_current_task->fds=&nas_current_task->fds_owned;
    nas_current_task->ppid = 0;
    nas_current_task->pgid = 1;
    nas_current_task->sid = 1;
    memcpy(nas_current_task->comm,"init",5);
    nas_current_task->umask = NAS_UMASK_DEFAULT;
    nas_current_task->state = NAS_TASK_RUNNING;
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
    if (next_pid > UINT32_MAX) return NULL; /* Linux user-visible pid_t */
    for (unsigned i = 1; i < NAS_TASK_LIMIT; i++) {
        if (task_table[i].state != NAS_TASK_FREE) continue;
        struct nas_task *child = &task_table[i];
        struct nas_fd_table inherited=*nas_current_task->fds;
        memcpy(child, nas_current_task, sizeof(*child));
        child->fds_owned=inherited;
        child->fds=&child->fds_owned;
        child->vm_owner=child;
        /* A fork from a worker snapshots the LIVE CLONE_FS and
         * CLONE_SIGHAND process state, not stale per-worker copies. */
        child->umask=nas_current_task->vm_owner->umask;
        memcpy(child->cwd,nas_current_task->vm_owner->cwd,sizeof(child->cwd));
        memcpy(child->signal_actions,nas_current_task->vm_owner->signal_actions,
               sizeof(child->signal_actions));
        /* fork copies userspace state but NEVER the parent's stack owner. */
        stack_reset(i);
        child->kernel_stack_top = stack_top(i);
        child->pid = next_pid++;
        child->tgid=child->pid;
        child->is_thread=false;
        child->group_exit_pending=false;
        child->ppid = nas_current_task->pid;
        /* pgid and sid were inherited from parent; execed is per-fork. */
        child->execed = false;
        child->state = NAS_TASK_READY;
        child->cpu_runtime_ns=0;
        child->exit_code = 0;
        child->termination_signal = 0;
        child->stopped_from = NAS_TASK_FREE;
        child->stop_report_pending = false;
        child->continue_report_pending = false;
        child->stop_signal = 0;
        child->resume_valid = false;
        /* Fork inherits resources, never pending waits or device watches. */
        nas_task_wait_reset(child);
        /* POSIX timers are not inherited across fork. */
        for(unsigned n=0;n<NAS_POSIX_TIMERS;n++)child->timers[n]=(struct nas_posix_timer){0};
        child->signal_frame_active = false;
        child->signal_frame_sp = 0;
        child->ppoll_mask_active=false;
        child->signal_force_deliver=0;
        /* Fork creates a new thread-group / task identity. The parent's
         * clear_child_tid address belongs only to the parent's thread. */
        child->thread_clear_tid = 0;
        child->robust_list_head = 0; /* clone/fork must register its own list */
        child->vfork_child_pid = 0;
        child->vfork_parent_pid = 0;
        child->vfork_shared_vm = false;
        child->pending_signals = 0; /* pending signals are NOT inherited by fork */
        return child;
    }
    return NULL;
}
void nas_task_adopt_orphans(uint64_t exiting_pid) {
    if (exiting_pid <= 1 || task_table[0].pid != 1 ||
        task_table[0].state == NAS_TASK_FREE) return;
    for (unsigned i=1; i<NAS_TASK_LIMIT; ++i) {
        struct nas_task *child=&task_table[i];
        if (child->state != NAS_TASK_FREE &&
            child->ppid == exiting_pid) child->ppid=1;
    }
}
void nas_task_release(struct nas_task *task) {
    if (!task || task == nas_current_task) return;
    task->state = NAS_TASK_FREE;
    task->pid = 0;
    task->tgid=0;
    task->is_thread=false;
    task->group_exit_pending=false;
    task->vm_owner=task;
    task->fds=&task->fds_owned;
    task->robust_list_head=0;
    nas_task_wait_reset(task);
    task->ppid = 0;
    task->pgid = 0;
    task->sid = 0;
    task->umask = NAS_UMASK_DEFAULT;
    task->execed = false;
    task->stopped_from = NAS_TASK_FREE;
    task->stop_report_pending = false;
    task->continue_report_pending = false;
    task->stop_signal = 0;
    task->pending_signals = 0;
    task->vfork_child_pid = task->vfork_parent_pid = 0;
    task->vfork_shared_vm = false;
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
    return task ? nas_arch_context_tls_base(&task->arch) : 0;
}
void nas_task_set_root(struct nas_task *task, uint64_t root) {
    if (task) task->root_phys = root;
}
struct nas_task *nas_task_child_at(uint64_t parent_pid, unsigned slot) {
    if (slot >= NAS_TASK_LIMIT || !parent_pid) return NULL;
    struct nas_task *task = &task_table[slot];
    if (task->state == NAS_TASK_FREE || (task->ppid != parent_pid || task->is_thread))
        return NULL;
    return task;
}

struct nas_task *nas_task_at(unsigned slot) {
    return slot < NAS_TASK_LIMIT ? &task_table[slot] : NULL;
}
uint8_t nas_task_fd_kind(const struct nas_task *task, uint64_t fd) {
    return task && fd < NAS_FD_LIMIT ? task->fds->entries[fd].kind : 0;
}
unsigned nas_task_fd_slot(const struct nas_task *task, uint64_t fd) {
    return task && fd < NAS_FD_LIMIT ? task->fds->entries[fd].slot : NAS_OPEN_FILE_LIMIT;
}
