#ifndef NASEED_TASK_H
#define NASEED_TASK_H
#include <stdint.h>
#include <stdbool.h>

/* M6.1: ownership boundaries for real user processes.
 * NaOS (2025-04-26) initially separated PID/PPID, state, page root,
 * CPU context, cwd and FDs. NaSeed first establishes the same lifetime
 * boundaries, without pretending fork() or scheduling already works.
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
    uint8_t file_refcount[NAS_FD_LIMIT];
    uint8_t fd_cloexec[NAS_FD_LIMIT];
    uint64_t file_status_flags[NAS_FD_LIMIT];
    uint64_t boot_file_offsets[NAS_FD_LIMIT];
    const struct limine_file *boot_files[NAS_FD_LIMIT];
    const struct bootfs_entry *boot_entries[NAS_FD_LIMIT];
};

struct nas_task {
    uint64_t pid, ppid;
    enum nas_task_state state;
    uint32_t exit_code;

    /* One independent VM mapping root and ELF/brk/mmap tracking per task. */
    uint64_t root_phys, heap_break, mmap_cursor;
    uint64_t loaded_phdr, loaded_phnum;
    uint64_t loaded_main_entry, loaded_interp_base;
    uint64_t user_fs_base, thread_clear_tid;

    /* Per-process signal disposition/mask and filesystem context. */
    uint64_t blocked_signals;
    uint64_t signal_actions[65][4];
    char cwd[256];
    struct nas_fd_table fds;

    /* Current M6.1 only: real user-thread lifecycle, no scheduler yet. */
    bool running_bash, running_busybox, running_alpine;
    bool saw_bash_hello;
    unsigned matched_chars;
};

/* Called before paging_init. get_current is non-null afterwards. */
extern struct nas_task *nas_current_task;
void nas_task_boot_init(void);
struct nas_task *nas_task_current(void);
const struct nas_task *nas_task_find(uint64_t pid);
#endif
