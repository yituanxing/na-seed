#ifndef NASEED_FD_H
#define NASEED_FD_H
#include <stdbool.h>
#include <stdint.h>

/* Per-task descriptor entry and global shared open-description lifetime.
 * Descriptor table fields stay in nas_task; offset/status and reference
 * identity are shared by fork and dup, not copied per descriptor.
 * A return value of NAS_OPEN_FILE_LIMIT means no final description was released.
 */
struct nas_task;
/* System-wide open descriptions, independent of per-process FD count. */
#define NAS_OPEN_FILE_LIMIT 128
/* Shared FD kind identities until the file-object refactor. */
enum { FD_CLOSED = 0, FD_UART = 1, FD_NULL = 2, FD_ZERO = 3,
       FD_BOOTFS = 4, FD_ARCHIVE = 5, FD_DIR = 6,
       FD_PIPE_R = 7, FD_PIPE_W = 8, FD_TMP = 9, FD_TMP_DIR = 10,
       FD_TTY = 11, FD_EPOLL = 12, FD_EVENTFD = 13,
       FD_SOCKET = 14, FD_PTY_MASTER = 15, FD_PTY_SLAVE = 16 };
/* Descriptor-local entry. The open-file description is a separate
 * refcounted object: dup/fork share its offset, not the CLOEXEC bit. */
struct nas_fd_entry {
    uint8_t kind;
    uint8_t slot;
    uint8_t cloexec;
};
_Static_assert(sizeof(struct nas_fd_entry) == 3, "FD entry must be 3 bytes");
void nas_fd_system_init(void);
void nas_fd_report_exhaustion(const char *operation);
/* Ownership-preserving placement API, shared by boot, regular files,
 * pipes, socket, eventfd and epoll.
 * reserve is read-only: it checks capacity without exposing partial FDs.
 * attach_owned transfers a previously accounted open-description ref
 * (AF_UNIX accept/SCM_RIGHTS), without incrementing it again. */
unsigned nas_fd_first_unused(const struct nas_task *task, unsigned minimum);
int nas_fd_reserve(const struct nas_task *task, unsigned minimum, unsigned count,
                   unsigned fds[2], unsigned slots[2], const char *operation);
bool nas_fd_install_new(struct nas_task *task, unsigned fd, unsigned slot,
                        uint8_t kind, uint64_t status_flags, bool cloexec);
bool nas_fd_attach_owned(struct nas_task *task, unsigned fd, unsigned slot,
                         uint8_t kind, bool cloexec);
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
extern struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
#endif
