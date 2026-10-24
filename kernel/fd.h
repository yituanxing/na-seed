#ifndef NASEED_FD_H
#define NASEED_FD_H
#include <stdbool.h>
#include <stdint.h>

/* Per-task descriptor entry and global shared open-description lifetime.
 * Descriptor table fields stay in nas_task; offset/status and reference
 * identity are shared by fork and dup, not copied per descriptor.
 * A return value of NAS_FD_LIMIT means no final description was released.
 */
struct nas_task;
/* Shared FD kind identities until the file-object refactor. */
enum { FD_CLOSED = 0, FD_UART = 1, FD_NULL = 2, FD_ZERO = 3,
       FD_BOOTFS = 4, FD_ARCHIVE = 5, FD_DIR = 6,
       FD_PIPE_R = 7, FD_PIPE_W = 8, FD_TMP = 9, FD_TMP_DIR = 10,
       FD_TTY = 11, FD_EPOLL = 12, FD_EVENTFD = 13 };
void nas_fd_system_init(void);
unsigned nas_fd_close(struct nas_task *task, unsigned fd);
uint64_t nas_fd_clone(struct nas_task *task, uint64_t src, uint64_t dst,
                      bool cloexec); /* caller must close dst beforehand */
uint64_t nas_fd_duplicate(struct nas_task *task, uint64_t src,
                          uint64_t minimum, bool cloexec);
bool nas_task_hold_files(const struct nas_task *task);

/* Open-file descriptions are shared across fork/dup. FD_CLOEXEC and the
 * per-task mapping of FD number -> description remain task-local. This is a
 * fixed-capacity object table for now, not a heap allocator or full VFS. */
struct nas_file_ops;
struct limine_file;
struct bootfs_entry;
struct nas_open_file {
    uint16_t refcount;
    uint64_t status_flags;
    uint64_t position;
    const struct nas_file_ops *ops;
    const struct limine_file *boot_file;
    const struct bootfs_entry *boot_entry;
};
extern struct nas_open_file nas_open_files[64];
#endif
