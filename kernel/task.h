#ifndef NASEED_TASK_H
#define NASEED_TASK_H
#include <stdint.h>
#include <stdbool.h>
#include "arch/context.h"

/* NaOS-informed ownership boundaries for x86-64 user tasks.
 * A single kernel stack is shared, so cooperative switching occurs only
 * at the syscall boundary; each task retains its own user CPU/VM state.
 */
#define NAS_TASK_LIMIT 4
#define NAS_FD_LIMIT 64

struct limine_file;
struct bootfs_entry;

enum nas_task_state {
    NAS_TASK_FREE = 0,
    NAS_TASK_READY,
    NAS_TASK_RUNNING,
    NAS_TASK_BLOCKED,
    NAS_TASK_ZOMBIE
};

struct nas_fd_table {
    uint8_t descriptors[NAS_FD_LIMIT];
    uint8_t file_slot[NAS_FD_LIMIT];
    uint8_t fd_cloexec[NAS_FD_LIMIT];
    const struct limine_file *boot_files[NAS_FD_LIMIT];
    const struct bootfs_entry *boot_entries[NAS_FD_LIMIT];
};

struct nas_task {
    uint64_t pid, ppid;
    enum nas_task_state state;
    uint32_t exit_code;
    uint32_t termination_signal; /* Linux wait4 WIFSIGNALED status */
    /* Saved syscall-boundary GPR frame in the exact arch.S push order.
     * The scheduler switches after unwinding the current syscall frame.
     */
    struct nas_arch_context arch;
    bool resume_valid;
    /* Pending blocking syscall holds user pointers, but never kernel-stack
     * pointers. The task's private CR3 must be restored before replay.
     */
    uint64_t blocked_op, blocked_args[4];
    bool pending_valid; /* read(0) is a real pending syscall, not 'none' */

    /* One independent VM mapping root and ELF/brk/mmap tracking per task. */
    uint64_t root_phys, heap_break, mmap_cursor;
    uint64_t loaded_phdr, loaded_phnum;
    uint64_t loaded_main_entry, loaded_interp_base;
    uint64_t thread_clear_tid;

    /* Per-process signal disposition/mask and filesystem context. */
    uint64_t blocked_signals;
    uint64_t signal_actions[65][4];
    char cwd[256];
    struct nas_fd_table fds;

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
struct nas_task *nas_task_current(void);
struct nas_task *nas_task_fork_slot(void);
void nas_task_release(struct nas_task *task);
uint64_t nas_task_page_root(const struct nas_task *task);
uint64_t nas_task_tls_base(const struct nas_task *task);
void nas_task_set_root(struct nas_task *task, uint64_t root);
struct nas_task *nas_task_by_pid(uint64_t pid);
struct nas_task *nas_task_child_at(uint64_t parent_pid, unsigned slot);
struct nas_task *nas_task_at(unsigned slot);
uint8_t nas_task_fd_kind(const struct nas_task *task, uint64_t fd);
unsigned nas_task_fd_slot(const struct nas_task *task, uint64_t fd);
const struct nas_task *nas_task_find(uint64_t pid);
#endif
