#include "task.h"
#include "fs/mode.h"
#include "arch/abi.h"
#include "fd.h"
#include "task/wait.h"
#include "lib/string.h"
#include "panic.h"
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
    nas_mm_system_init();
    nas_resources_system_init();
    nas_fd_system_init();
    nas_current_task = &task_table[0];
    nas_current_task->pid = 1;
    nas_current_task->tgid=1;
    nas_current_task->group_leader=nas_current_task;
    nas_current_task->mm=nas_mm_new();
    if (!nas_current_task->mm) nas_fail("cannot allocate init MM");
    nas_current_task->files=nas_files_new();
    if(!nas_current_task->files)nas_fail("cannot allocate init files");
    nas_current_task->fds=&nas_current_task->files->table;
    nas_current_task->ppid = 0;
    nas_current_task->pgid = 1;
    nas_current_task->sid = 1;
    memcpy(nas_current_task->comm,"init",5);
    nas_current_task->fs=nas_fs_context_new();
    nas_current_task->sighand=nas_sighand_new();
    if(!nas_current_task->fs || !nas_current_task->sighand)
        nas_fail("cannot allocate init process resources");
    nas_current_task->state = NAS_TASK_RUNNING;
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
        struct nas_fs_context *child_fs=nas_fs_context_copy(nas_current_task->fs);
        if(!child_fs) return NULL;
        struct nas_sighand *child_sighand=nas_sighand_copy(nas_current_task->sighand);
        if(!child_sighand) { nas_fs_context_put(child_fs); return NULL; }
        struct nas_files *child_files=nas_files_copy(nas_current_task->files);
        if(!child_files) {
            nas_fs_context_put(child_fs);
            nas_sighand_put(child_sighand);
            return NULL;
        }
        memcpy(child, nas_current_task, sizeof(*child));
        child->files=child_files;
        child->fds=&child_files->table;
        child->group_leader=child;
        child->fs=child_fs;
        child->sighand=child_sighand;
        /* memcpy must never transfer a reference without incrementing it. */
        child->mm=0;
        /* A fork from a worker snapshots the LIVE CLONE_FS and
         * CLONE_SIGHAND process state, not stale per-worker copies. */

        /* fork copies userspace state but NEVER the parent's stack owner. */
        stack_reset(i);
        child->kernel_stack_top = stack_top(i);
        /* fork memcpy may copy a live parent saved SP/queue link. Both
         * belong to the old executing thread and must be reset together. */
        nas_kernel_execution_reset(&child->kernel_exec);
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
        child->ptrace_tracer_pid = 0;
        child->ptrace_options=0;
        child->ptrace_phase=0;
        child->ptrace_trace_syscalls=false;
        child->ptrace_skip_entry_once=false;
        child->ptrace_event=0;
        child->ptrace_event_message=0;
        child->ptrace_exec_pending_exit=false;
        child->ptrace_exit_event_sent=false;
        child->ptrace_child_initial_stop=false;
        child->ptrace_fork_pending_exit=false;
        child->ptrace_blocked_exit_pending=false;
        child->ptrace_single_step=false;
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
        /* ptrace is a *tracer* relationship, not inherited by init:
         * when tracer dies, detach and restore runnable tracees instead
         * of leaving a permanently stopped task without an owner. */
        if (child->state!=NAS_TASK_FREE &&
            child->ptrace_tracer_pid==exiting_pid) {
            child->ptrace_tracer_pid=0;
            child->ptrace_options=0;
            child->ptrace_trace_syscalls=false;
            if(child->state==NAS_TASK_STOPPED &&
               (child->ptrace_phase || child->ptrace_child_initial_stop || child->ptrace_event)) {
                child->state=NAS_TASK_READY;
                child->stopped_from=NAS_TASK_FREE;
                child->stop_report_pending=false;
            }
            child->ptrace_phase=0;
            child->ptrace_event=0;
            child->ptrace_child_initial_stop=false;
            child->ptrace_fork_pending_exit=false;
            child->ptrace_blocked_exit_pending=false;
        }
    }
}
void nas_task_release(struct nas_task *task) {
    if (!task || task == nas_current_task) return;
    /* An abandoned kernel continuation must never leave a dangling waiter. */
    (void)nas_wait_queue_remove(&task->kernel_exec.wait);
    /* Releases an independently owned MM reference, including on thread exit. */
    if (task->mm) { nas_mm_put(task->mm); task->mm=0; }
    if (task->fs) { nas_fs_context_put(task->fs); task->fs=0; }
    if (task->sighand) { nas_sighand_put(task->sighand); task->sighand=0; }
    if (task->files) { nas_files_put(task->files); task->files=0; }
    task->state = NAS_TASK_FREE;
    task->pid = 0;
    task->tgid=0;
    task->is_thread=false;
    task->group_exit_pending=false;
    task->group_leader=task;
    task->fds=0;
    task->robust_list_head=0;
    nas_task_wait_reset(task);
    nas_kernel_execution_reset(&task->kernel_exec);
    task->ppid = 0;
    task->pgid = 0;
    task->sid = 0;
    task->execed = false;
    task->stopped_from = NAS_TASK_FREE;
    task->stop_report_pending = false;
    task->continue_report_pending = false;
    task->stop_signal = 0;
    task->ptrace_tracer_pid = 0;
    task->ptrace_options=0;
    task->ptrace_phase=0;
    task->ptrace_trace_syscalls=false;
    task->ptrace_skip_entry_once=false;
    task->ptrace_event=0;
    task->ptrace_event_message=0;
    task->ptrace_exec_pending_exit=false;
    task->ptrace_exit_event_sent=false;
    task->ptrace_child_initial_stop=false;
    task->ptrace_fork_pending_exit=false;
    task->ptrace_blocked_exit_pending=false;
    task->ptrace_single_step=false;
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
    return task && task->mm ? task->mm->root_phys : 0;
}
uint64_t nas_task_tls_base(const struct nas_task *task) {
    return task ? nas_arch_context_tls_base(&task->arch) : 0;
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
