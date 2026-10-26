#ifndef NASEED_TASK_H
#define NASEED_TASK_H
#include <stdint.h>
#include <stdbool.h>
#include "fd.h"
#include "arch/context.h"
#include "mm/mm.h"
#include "task/resources.h"

/* Common process registry across x86-64, RV64, and AArch64.
 * Each task has its own kernel entry stack; switching still occurs at a
 * syscall boundary; each task retains its own user CPU/VM state.
 * Process slots are finite; exhaustion returns Linux EAGAIN from fork.
 * A 48-task bounded registry supports GNU Make -j4, GNU xargs -P4 and
 * 24+ concurrently parked fork children with independent 16 KiB stacks.
 * Scheduler/exit/futex/group-signal loops follow this shared limit;
 * open-file-description pool has its own independent capacity.
 */
#define NAS_TASK_LIMIT 48
#define NAS_POSIX_TIMERS 8
struct nas_posix_timer {
    uint64_t deadline_ns,interval_ns,thread_tid;
    uint32_t clockid,signum;
    int32_t overrun;
    uint8_t notify;
    bool used,armed;
};
#define NAS_FD_LIMIT 64
#define NAS_KERNEL_STACK_BYTES 16384U

/* Blocked I/O watches system-wide open-description slots, not process FDs.
 * Word-indexed masks preserve correct readiness above slot 63. */
#define NAS_SLOT_MASK_WORDS ((NAS_OPEN_FILE_LIMIT + 63U) / 64U)
struct nas_slot_mask { uint64_t word[NAS_SLOT_MASK_WORDS]; };
static inline void nas_slot_mask_clear(struct nas_slot_mask *mask) {
    for (unsigned i=0; i<NAS_SLOT_MASK_WORDS; i++) mask->word[i]=0;
}
static inline void nas_slot_mask_set(struct nas_slot_mask *mask,unsigned slot) {
    if (slot<NAS_OPEN_FILE_LIMIT) mask->word[slot/64U] |= 1ULL<<(slot%64U);
}
static inline bool nas_slot_mask_test(const struct nas_slot_mask *mask,unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT &&
        (mask->word[slot/64U] & (1ULL<<(slot%64U)))!=0;
}
static inline bool nas_slot_mask_any(const struct nas_slot_mask *mask) {
    for (unsigned i=0; i<NAS_SLOT_MASK_WORDS; i++)
        if (mask->word[i]) return true;
    return false;
}


struct limine_file;
struct bootfs_entry;

enum nas_task_state {
    NAS_TASK_FREE = 0,
    NAS_TASK_READY,
    NAS_TASK_RUNNING,
    NAS_TASK_BLOCKED,
    NAS_TASK_ZOMBIE,
    NAS_TASK_STOPPED
};

/* Copy on fork, share pointer under CLONE_FILES. No parallel FD arrays. */
struct nas_fd_table {
    struct nas_fd_entry entries[NAS_FD_LIMIT];
};

/* An executing thread owns ALL transient blocking state, never process VM.
 * The scheduler cannot keep kernel C frames and must replay after VM restore. */
struct nas_task_wait {
    uint64_t blocked_op, blocked_args[4];
    bool pending_valid;
    uint64_t deadline_ns;
    bool deadline_active;
    unsigned tty_observed_bytes;
    struct nas_slot_mask poll_pipe_read, poll_pipe_write;
    bool poll_tty_read;
    uint64_t futex_addr;
    uint32_t futex_mask;
    bool futex_woken;
};

struct nas_task {
    uint64_t pid, ppid;
    uint64_t tgid; /* getpid: thread group; pid remains unique gettid */
    bool is_thread; /* CLONE_THREAD tasks are never wait4 children */
    bool group_exit_pending; /* leader exited via SYS_exit; retain VM and FDs for peers */
    struct nas_task *vm_owner; /* legacy thread-group leader identity; NOT MM/FS/files/sighand owner */
    /* Linux process group + session identity, independent of CPU/VM. */
    uint64_t pgid, sid;
    char comm[16]; /* Linux per-thread PR_SET_NAME, inherited at fork. */
    bool execed; /* parent may not setpgid(child) after successful exec */
    /* Immutable executable argv snapshot; incomplete overlong vectors
     * are not exposed as fabricated procfs command lines. */
    char commandline[1024];
    uint16_t commandline_bytes;
    /* Single-CPU elapsed Task occupancy, including syscall kernel time;
     * does not pretend to separate user/system CPU instructions. */
    uint64_t cpu_runtime_ns;
    enum nas_task_state state;
    uint32_t exit_code;
    uint32_t termination_signal; /* Linux wait4 WIFSIGNALED status */
    /* Stop is orthogonal to an outstanding, parked syscall. Save whether
     * SIGCONT restores READY (runnable) or BLOCKED (pending replay). */
    enum nas_task_state stopped_from;
    bool stop_report_pending, continue_report_pending;
    /* ptrace ownership is TASK-local; not inherited by fork or CLONE_THREAD.
     * A tracer sees real SIGSTOP events through its existing wait4 owner. */
    uint64_t ptrace_tracer_pid;
    uint64_t ptrace_options;
    uint64_t ptrace_call[7]; /* raw native Linux nr + six register args */
    uint64_t ptrace_ip,ptrace_sp,ptrace_result;
    /* phase: 0=signal/other, 1=syscall entry, 2=syscall exit. */
    uint8_t ptrace_phase;
    bool ptrace_trace_syscalls,ptrace_skip_entry_once;
    uint8_t ptrace_event; /* Linux PTRACE_EVENT_*; zero when no event */
    uint64_t ptrace_event_message;
    bool ptrace_exec_pending_exit,ptrace_exit_event_sent;
    /* Traced child creation and true sleeping syscall completions. */
    bool ptrace_child_initial_stop,ptrace_fork_pending_exit;
    bool ptrace_blocked_exit_pending;
    /* One hardware user instruction per request; not inherited by fork. */
    bool ptrace_single_step;
#if defined(__riscv) && __riscv_xlen==64
    /* Single-core RV64 software single-step: one temp instruction in the
     * stopped tracee's owned VM, restored BEFORE reporting SIGTRAP. */
    uint64_t ptrace_step_addr;
    uint32_t ptrace_step_saved;
    uint8_t ptrace_step_width;
#endif
    uint32_t stop_signal;
    /* Native CPU exception siginfo for ptrace GETSIGINFO. Zeroed at
     * non-exception stops; actual #BP/#DB/BRK/EBREAK supply Linux si_code. */
    int32_t ptrace_signal_code;
    uint64_t ptrace_signal_addr;
    /* Saved syscall-boundary GPR frame in the exact arch.S push order.
     * The scheduler switches after unwinding the current syscall frame.
     */
    struct nas_arch_context arch;
    bool resume_valid;
    /* Pending blocking syscall holds user pointers, but never kernel-stack
     * pointers. The task's private CR3 must be restored before replay.
     */
    struct nas_task_wait wait; /* thread-local blocked syscall/deadline/I/O */
    struct nas_posix_timer timers[NAS_POSIX_TIMERS];
    /* Snapshot poll interest when blocking. NEVER dereference a different
     * task's user pollfd pointer during readiness checks. Each bit names a
     * shared open-file-description slot (0..NAS_OPEN_FILE_LIMIT-1). */
    /* Poll interest snapshots live in wait, not CPU context. */

    /* Owned kernel entry stack. Kernel C continuations are NOT resumable
     * yet; the syscall-boundary scheduler still unwinds before switching. */
    uint64_t kernel_stack_top;
    /* Independently reference-counted address space (CLONE_VM / vfork). */
    struct nas_mm *mm;
    uint64_t loaded_phdr, loaded_phnum;
    uint64_t loaded_main_entry, loaded_interp_base;
    uint64_t thread_clear_tid;
    uint64_t robust_list_head; /* per-TID user robust_list_head, not inherited */
    /* Futex wait ownership lives in task.wait. */
    /* A vfork parent sleeps until exec/exit; child holds shared-VM root. */
    uint64_t vfork_child_pid, vfork_parent_pid;
    bool vfork_shared_vm;

    /* Signal dispositions, signal mask, and FS context have distinct lifetimes. */
    uint64_t blocked_signals;
    uint64_t pending_signals; /* Linux per-task pending standard signals */
    struct nas_sighand *sighand; /* CLONE_SIGHAND shares this owner */
    /* While executing a caught handler, the original full CPU/FPU state
     * remains kernel-owned until a checked rt_sigreturn from its restorer.
     * This deliberately supports one handler frame; further signals
     * remain pending until that frame returns. */
    bool signal_frame_active;
    uint64_t signal_frame_sp, signal_saved_mask;
    /* ppoll installs a temporary signal mask atomically for the wait.
     * Its original mask is restored on ALL completion paths, but a
     * pending caught signal must still be delivered after EINTR. */
    uint64_t ppoll_saved_mask;
    bool ppoll_mask_active;
    uint64_t signal_force_deliver;
    struct nas_arch_context signal_saved_arch;
    struct nas_fs_context *fs; /* CLONE_FS shares this owner */
    struct nas_fd_table fds_owned;
    struct nas_fd_table *fds; /* CLONE_FILES shares descriptor TABLE */

    /* Real user-thread lifecycle and per-task syscall-boundary state. */
    bool running_bash, running_busybox, running_alpine;
    bool saw_bash_hello;
    unsigned matched_chars;
};

/* Called before paging_init. get_current is non-null afterwards.
 * Shared open-file-description state and hold-on-fork live in fd.h.
 */
extern struct nas_task *nas_current_task;
void nas_task_boot_init(void);
void nas_task_set_commandline(struct nas_task *,const char *const *,unsigned);
void nas_task_cpu_account_switch(struct nas_task *);
void nas_task_cpu_account_snapshot(uint64_t *busy_ns,uint64_t *idle_ns,
                                   uint64_t load_q16[3],uint64_t *switches);
bool nas_task_kernel_stack_ok(const struct nas_task *task);
struct nas_task *nas_task_current(void);
struct nas_task *nas_task_fork_slot(void);
void nas_task_release(struct nas_task *task);
/* Linux orphan semantics: children of an exiting non-init task are adopted
 * by PID 1 (which is responsible for eventually wait4-reaping zombies). */
void nas_task_adopt_orphans(uint64_t exiting_pid);
uint64_t nas_task_page_root(const struct nas_task *task);
uint64_t nas_task_tls_base(const struct nas_task *task);
struct nas_task *nas_task_by_pid(uint64_t pid);
struct nas_task *nas_task_child_at(uint64_t parent_pid, unsigned slot);
struct nas_task *nas_task_at(unsigned slot);
uint8_t nas_task_fd_kind(const struct nas_task *task, uint64_t fd);
unsigned nas_task_fd_slot(const struct nas_task *task, uint64_t fd);
const struct nas_task *nas_task_find(uint64_t pid);
#endif
