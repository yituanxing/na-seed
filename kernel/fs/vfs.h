#ifndef NASEED_VFS_H
#define NASEED_VFS_H
#include <stdint.h>
#include <stdbool.h>
/* Registered VFS providers; kernel-owned buffers only, never Linux user ABI.
 * nas_fs_node is a borrowed inode snapshot, not an owning open file. */
struct nas_fs_node;
struct nas_vfs_mount;
struct nas_posix_locks;
/* Backend-owned directory metadata in kernel memory, independent of
 * Linux userspace dirent64 layout or any concrete filesystem structures. */
struct nas_vfs_dirent {
    uint64_t ino;
    uint8_t dtype;
    uint8_t name_len;
    char name[256];
};
struct nas_vfs_type {
    const char *name;
    /* Human-readable source label for stable exec acceptance diagnostics;
     * does not participate in dispatch or inode ownership. */
    const char *exec_source_label;
    /* VFS owns pathname walking and mount dispatch. Filesystems implement
     * root inode acquisition plus one-name lookup beneath a parent inode.
     * No callback receives an absolute VFS pathname for lookup. */
    int (*root)(const struct nas_vfs_mount *,struct nas_fs_node *);
    int (*lookup_child)(const struct nas_fs_node *parent,const char *name,
                        struct nas_fs_node *out);
    /* Symlink contents are inode data, not an old pathname (Linux
     * i_op->get_link, NaOS vfs_inode operation). A renamed symlink keeps
     * the same content across open handles and mount aliases. */
    int (*readlink)(const struct nas_fs_node *,char target[256]);
    /* Optional inode->LIVE namespace path for directory handles. The
     * provider owns inode/dentry knowledge; the syscall ABI never does.
     * A NULL callback means VFS can verify an immutable opening hint, but
     * refuses to redirect renamed directory FDs to an unrelated name. */
    int (*directory_path)(const struct nas_fs_node *,char absolute[256]);
    /* Called precisely when the LAST generic mount+inode lease is dropped.
     * A backend may now reclaim an unlinked orphan inode; never called
     * on a borrowed namei snapshot or one of several dup/fork owners. */
    void (*last_inode_unpin)(const struct nas_fs_node *);
    /* Live metadata from the pinned inode, never reopening its old path.
     * Refresh fields only: inode, ino, filesystem, mount remain immutable. */
    int (*getattr)(const struct nas_fs_node *,struct nas_fs_node *out);
    int64_t (*read_at)(const struct nas_fs_node *, uint64_t offset,
                       uint8_t *kernel_dst, uint64_t length);
    /* Logical cursor counts entries, including . and .., from zero.
     * Returns count, 0 at EOF, or negative errno. Batch size is bounded. */
    /* Directory enumeration is inode-owned: never reopen an old FD
     * pathname, including /proc/self after fork or directory rename. */
    int (*read_dir)(const struct nas_fs_node *,uint64_t cursor,
                    struct nas_vfs_dirent *entries,unsigned maximum);
    int (*chmod)(const struct nas_fs_node *, uint64_t mode);
    int (*utimens)(const struct nas_fs_node *, uint64_t atime,
                   uint64_t mtime, bool set_atime, bool set_mtime);
    /* Namespace mutation ABI: providers receive owning PARENT inode(s)
     * and single child name(s), never a global VFS absolute pathname.
     * Backend-specific path reconstruction, if required by upstream APIs,
     * happens behind the port. VFS enforces mount identity and safety. */
    int (*create)(const struct nas_fs_node *parent,const char *name,uint64_t mode);
    int64_t (*write_at)(const struct nas_fs_node *,uint64_t offset,
                        const uint8_t *kernel_source,uint64_t length);
    int (*resize)(const struct nas_fs_node *,uint64_t size);
    int (*sync)(const struct nas_fs_node *);
    int (*mkdir)(const struct nas_fs_node *parent,const char *name,uint64_t mode);
    int (*unlink)(const struct nas_fs_node *parent,const char *name,uint64_t flags);
    int (*rename)(const struct nas_fs_node *old_parent,const char *old_name,
                  const struct nas_fs_node *new_parent,const char *new_name,
                  uint64_t flags);
    int (*link)(const struct nas_fs_node *old_parent,const char *old_name,
                const struct nas_fs_node *new_parent,const char *new_name);
    int (*symlink)(const struct nas_fs_node *parent,const char *name,
                   const char *target);
    /* Backends may opt in only if unlinking the final name does not
     * invalidate an open, pinned inode (tmpfs has this lifetime today). */
    bool unlink_open_safe;
    /* Replacement renames must preserve an open destination inode.
     * Default false: the provider must explicitly guarantee this. */
    bool rename_replace_open_safe;
    bool read_only;
};
struct nas_vfs_mount {
    const char *mountpoint; /* Static, stable absolute path */
    const struct nas_vfs_type *type;
    /* Legacy /tmp overlay can still expose CPIO descendants; real disk
     * mounts must never use this compatibility fallback. */
    bool fallback_on_miss;
    bool permanent; /* bootstrap filesystem mounts cannot be detached */
    bool read_only;  /* per-mount restriction */
    void *private_data; /* owning superblock/device adapter */
};
/* An owned File or FS cwd context pins a mount once per owner.
 * Dup/CLONE_FS share their owner; normal fork copies the pin. */
int nas_vfs_unregister_mount(const struct nas_vfs_mount *mount);
int nas_vfs_node_pin(const struct nas_fs_node *node);
int nas_vfs_node_unpin(const struct nas_fs_node *node);
/* Advisory locks are common inode/File services (Linux file_lock_context),
 * not private to tmpfs or encoded by FD_TMP. F_GETLK/F_SETLK process
 * ownership lives on the pinned VFS inode lease. flock belongs to the
 * shared open-file description: pass its opaque stable owner pointer.
 * No Linux user ABI or filesystem-specific struct crosses this boundary. */
struct nas_posix_locks *nas_vfs_posix_locks(const struct nas_fs_node *node);
void nas_vfs_release_posix_locks(const struct nas_fs_node *node,uint64_t pid);
int nas_vfs_flock(const struct nas_fs_node *node,const void *file_owner,
                  unsigned operation);
void nas_vfs_release_flock(const void *file_owner);
/* Opaque, already allocated inode identity: protects orphan reclamation
 * of cwd and independently pinned VFS Files, not merely legacy FD slots. */
bool nas_vfs_inode_is_pinned(const void *backend_inode);
uint64_t nas_vfs_node_device(const struct nas_fs_node *node);
/* Live size shared by independently opened Files for the same mount+inode.
 * Unlike borrowed pathname snapshots this survives rename and dup/fork. */
uint64_t nas_vfs_node_size(const struct nas_fs_node *node);
int nas_vfs_getattr(const struct nas_fs_node *node,struct nas_fs_node *out);
int nas_vfs_register_mount(const struct nas_vfs_mount *mount);
int nas_vfs_lookup(const char *absolute, struct nas_fs_node *node);
/* One canonical path component. VFS checks mount crossings and the special
 * bootstrap underlay; filesystem callbacks see only (parent,name).
 * `absolute` is used for VFS mount selection, NEVER given to a provider. */
int nas_vfs_lookup_step(const struct nas_fs_node *parent,const char *absolute,
                         const char *name,struct nas_fs_node *out);
int nas_vfs_readlink(const struct nas_fs_node *node,char target[256]);
int64_t nas_vfs_read_at(const struct nas_fs_node *node, uint64_t offset,
                         uint8_t *dst, uint64_t count);
int nas_vfs_read_dir(const struct nas_fs_node *node,
                     uint64_t cursor,struct nas_vfs_dirent *entries,
                     unsigned maximum);
/* Resolve current pathname of an OWNED directory inode, not a stale File
 * origin_path or task-local fd. Verifies mount+inode identity after namei.
 * Returns ESTALE when the provider cannot recover a moved dentry. */
int nas_vfs_directory_path(const struct nas_fs_node *node,
                           const char *origin_hint,char absolute[256]);
int nas_vfs_chmod(const struct nas_fs_node *node, uint64_t mode);
int nas_vfs_utimens(const struct nas_fs_node *node, uint64_t atime,
                    uint64_t mtime, bool set_atime, bool set_mtime);
bool nas_vfs_can_write(const struct nas_fs_node *node);
/* VFS owns the open inode transaction and policy, independent of Linux
 * native O_* bit positions or backend type. A successful call returns one
 * mount+inode reference that MUST be released on final File close. */
struct nas_vfs_open_options {
    bool follow_final,create,exclusive,truncate,path_only,directory_only;
    unsigned access; /* 0=O_RDONLY, 1=O_WRONLY, 2=O_RDWR */
    uint64_t create_mode;
};
int nas_vfs_open_inode(const char *absolute,
                       const struct nas_vfs_open_options *options,
                       struct nas_fs_node *owned,char canonical[256]);
int nas_vfs_create(const char *absolute,uint64_t mode);
int64_t nas_vfs_write_at(const struct nas_fs_node *node,uint64_t offset,
                         const uint8_t *source,uint64_t count);
int nas_vfs_resize(const struct nas_fs_node *node,uint64_t length);
int nas_vfs_sync(const struct nas_fs_node *node);
int nas_vfs_mkdir(const char *absolute,uint64_t mode);
int nas_vfs_unlink(const char *absolute,uint64_t flags);
int nas_vfs_rename(const char *old_absolute,const char *new_absolute,uint64_t flags);
int nas_vfs_link(const char *old_absolute,const char *new_absolute);
int nas_vfs_symlink(const char *target,const char *absolute);
#endif
