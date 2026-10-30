#ifndef NASEED_FD_H
#define NASEED_FD_H
#include <stdbool.h>
#include <stdint.h>
#include "fs/lookup.h"

/* Per-task descriptor entry and global shared open-description lifetime.
 * Descriptor table fields stay in nas_task; offset/status and reference
 * identity are shared by fork and dup, not copied per descriptor.
 * A return value of NAS_OPEN_FILE_LIMIT means no final description was released.
 */
struct nas_task;
/* System-wide open descriptions, independent of per-process FD count. */
#define NAS_OPEN_FILE_LIMIT 128
#define NAS_FD_LIMIT 64
/* Shared FD kind identities until the file-object refactor. */
enum { FD_CLOSED = 0, FD_UART = 1, FD_NULL = 2, FD_ZERO = 3,
       FD_BOOTFS = 4, FD_ARCHIVE = 5, FD_DIR = 6,
       FD_PIPE_R = 7, FD_PIPE_W = 8, FD_TMP = 9, FD_TMP_DIR = 10,
       FD_TTY = 11, FD_EPOLL = 12, FD_EVENTFD = 13,
       FD_SOCKET = 14, FD_PTY_MASTER = 15, FD_PTY_SLAVE = 16,
       FD_PROC_FILE = 17, FD_PROC_DIR = 18 };
/* Descriptor-local entry. The open-file description is a separate
 * refcounted object: dup/fork share its offset, not the CLOEXEC bit. */
struct nas_fd_entry {
    uint8_t kind;
    uint8_t slot;
    uint8_t cloexec;
};
_Static_assert(sizeof(struct nas_fd_entry) == 3, "FD entry must be 3 bytes");
struct nas_fd_table {
    struct nas_fd_entry entries[NAS_FD_LIMIT];
};
struct nas_files {
    unsigned refs;
    struct nas_fd_table table;
};
struct nas_files *nas_files_new(void);
struct nas_files *nas_files_copy(const struct nas_files *);
struct nas_files *nas_files_get(struct nas_files *);
void nas_files_put(struct nas_files *);
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
/* One kernel-owned File reference, independent of a process FD table.
 * Pending syscalls and SCM_RIGHTS must keep the original description alive
 * across close/dup2 until their own final release. */
bool nas_fd_hold_description(unsigned slot);

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
    /* Provider inode + mount pin owned by shared file description; pipes
     * and sockets leave these empty. fork/dup do not copy the inode view. */
    struct nas_fs_node vnode;
    bool vnode_pinned;
    /* Owned positive Dentry and mount Path for directory openat/fchdir.
     * The existing vnode pin remains the independent File/inode owner;
     * dup/fork share this Dentry via their shared open description. */
    struct nas_vfs_path vpath;
    const struct limine_file *boot_file;
    /* Legacy procfs shadow PID/TGID/FD fields and bootfs entry pointer
     * were removed: all mounted File identities live in vnode+mount.
     * Keep only the non-VFS Limine module's opaque boot_file pointer. */
    /* Canonical path of the open description, shared by dup/fork. */
    char origin_path[256];
};
extern struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
#endif
