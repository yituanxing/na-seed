#ifndef NASEED_TMPFS_H
#define NASEED_TMPFS_H
#include <stdbool.h>
#include <stdint.h>
/* Volatile tmpfs inode storage and namespace provider behind shared VFS.
 * Syscall/user memory and descriptor ownership stay outside this backend. */
/* Metadata is allocated in demand-paged inode slabs, not a fixed
 * global array of eight inodes. Memory exhaustion is reported by the
 * existing physical frame allocator, not an arbitrary source-level count. */
/* Sparse, demand-allocated two-level indirect 4-KiB page index.
 * Each file has ONE optional root frame of 512 index-frame addresses;
 * each index frame has 512 data-frame addresses. No 1-GiB BSS allocation:
 * root, index and data frames are allocated only on first write.
 * Reclaimed on truncate, unlink/close and partial shrinking. */
#define TMP_PAGE_SIZE 4096ULL
#define TMP_PAGE_ENTRIES 512U
#define TMP_INDEX_ROOT_ENTRIES 512U
#define TMP_FILE_CAP (TMP_PAGE_SIZE * (uint64_t)TMP_PAGE_ENTRIES * TMP_INDEX_ROOT_ENTRIES)

struct nas_tmp_dentry;
/* A directory maintains its own ordered child chain and a per-directory
 * forward readdir cursor. Namespace mutations invalidate the cached cursor;
 * callers may still seek to any ordinal and receive the correct entry. */
struct nas_tmp_directory {
    struct nas_tmp_dentry *first, *last, *seek;
    unsigned count, seek_index;
};

struct nas_tmp_file {
    bool used, is_dir;
    uint32_t nlink; /* directory-entry references, not open FD references */
    uint64_t ino; /* stable object identity, never derived from a pathname */
    uint64_t index_root; /* physical address of optional 512-entry root frame */
    /* Inode-owned allocation watermark; sparse size growth never advances it.
     * FIEMAP_EXTENT_LAST must not repeatedly scan the remainder of a file. */
    uint64_t highest_allocated_data_page_plus_one;
    uint64_t xattr_frame; /* optional inode-owned packed extended-attribute frame */
    uint64_t size;
    uint64_t atime_ns, mtime_ns, ctime_ns; /* UNIX realtime nanoseconds */
    uint32_t mode;
    uint32_t uid,gid; /* per-inode Linux ownership, shared through all names */
    /* Only directories have a namespace alias: hard-linking directories is
     * forbidden. Detached cwd inodes retain storage but no alias. */
    struct nas_tmp_dentry *directory_alias;
    struct nas_tmp_directory children;
    /* Reclaimed inode slots reuse stable slab addresses in O(1) time. */
    struct nas_tmp_file *free_next;
 };

/* A dentry names an inode. Multiple dentries may reference one regular file.
 * Only shared VFS namei accepts and resolves complete path strings. */
/* The dentry owns only (parent inode, basename) -> child inode.
 * Rename moves ONE dentry. All descendant names resolve through the
 * unchanged parent chain, never bulk-copying cached absolute paths. */
struct nas_tmp_dentry {
    bool used;
    const void *parent_inode;
    char name[256];
    struct nas_tmp_file *inode;
    /* Hashed (parent, basename) lookup, per-parent ordered children, and
     * an independent reusable slot list. A used dentry is in the first two
     * indexes; a free dentry is only in the slot list. */
    struct nas_tmp_dentry *hash_next;
    struct nas_tmp_dentry *directory_prev, *directory_next;
    struct nas_tmp_dentry *free_next;
};
const struct nas_tmp_dentry *tmp_child_entry_inode(const void *parent,unsigned index);
const void *tmp_dir_parent_inode(const void *directory);
/* Live tmpfs directory stat size comes from the namespace's sole child list.
 * The /tmp root borrows its inode from bootfs and has separate child state. */
uint64_t tmp_directory_stat_size(const struct nas_tmp_file *directory);
uint64_t tmp_root_directory_stat_size(void);
struct nas_tmp_file *tmp_create_at(const void *parent,const char *name,bool dir,uint32_t mode);
/* Only S_IFSOCK is currently supported. The published inode already has
 * its final type bits; failures return NULL without publishing a dentry. */
struct nas_tmp_file *tmp_mknod_at(const void *parent,const char *name,uint32_t mode);
int tmp_mkdir_at(const void *parent,const char *name,uint64_t mode);
int tmp_unlink_at(const void *parent,const char *name,uint64_t flags);
int tmp_rename_at(const void *old_parent,const char *old_name,
                  const void *new_parent,const char *new_name,uint64_t flags);
int tmp_link_at(const void *old_parent,const char *old_name,
                const void *new_parent,const char *new_name);
int tmp_symlink_at(const void *parent,const char *name,const char *target);
struct nas_tmp_file *tmp_inode_alloc(bool dir,uint32_t mode);
void tmp_inode_link(struct nas_tmp_file *file);
void tmp_inode_unlink(struct nas_tmp_file *file);
void tmp_inode_discard(struct nas_tmp_file *file);
void tmp_inode_touch(struct nas_tmp_file *file);
/* A successful namespace edit modifies its containing directory, not merely
 * the inserted/unlinked child. The /tmp root borrows a bootfs inode and
 * therefore keeps its own mutable timestamp state behind this provider. */
void tmp_dir_modified(struct nas_tmp_file *directory);
void tmp_root_dir_modified(void);
void tmp_root_times(uint64_t *mtime_ns,uint64_t *ctime_ns);
/* Provider-local directory child lookup; the generic VFS passes only an
 * opaque parent inode and one component, never an absolute pathname. */
struct nas_tmp_file *tmp_find_child(const void *parent_inode,const char *name);
bool tmp_is_link(const struct nas_tmp_file *file);
int tmp_link_target(const struct nas_tmp_file *file, char target[256]);
uint64_t tmp_number(const struct nas_tmp_file *file);

/* Open-inode lifetime and last-unpin reclaim are owned solely by VFS. */
/* Kernel-buffer I/O, shared across Linux FD syscalls and ELF image reader.
 * Read of sparse holes returns zero; write allocates pages only on demand.
 * Negative errno only if no bytes were written. */
int64_t tmp_read_at(const struct nas_tmp_file *file, uint64_t offset,
                    uint8_t *dst, uint64_t count);
int64_t tmp_write_at(struct nas_tmp_file *file, uint64_t offset,
                     const uint8_t *src, uint64_t count);
/* Acquire a second owner ref to the existing sparse-file data frame for
 * direct MAP_SHARED PTE ownership. Offset is page-aligned and must be
 * below live i_size; the inode retains its own storage-frame reference. */
int tmp_acquire_shared_page(struct nas_tmp_file *file,uint64_t offset,
                            uint64_t *pa_owned);
int tmp_touch_shared(struct nas_tmp_file *file);
void tmp_truncate(struct nas_tmp_file *file);
int tmp_resize(struct nas_tmp_file *file,uint64_t size);
int tmp_preallocate(struct nas_tmp_file *file,uint64_t offset,
                    uint64_t length,unsigned mode);
int tmp_utimens(struct nas_tmp_file *file, uint64_t access_ns,
                uint64_t modified_ns, bool set_access, bool set_modified);

int tmp_chmod(struct nas_tmp_file *file, uint64_t mode);
int tmp_chown(struct nas_tmp_file *file,uint32_t uid,uint32_t gid);
/* A bounded, compact per-inode user.* namespace, stored with the inode and
 * released only after its final unlink AND VFS File/Path lease. */
int tmp_setxattr(struct nas_tmp_file *file,const char *name,
                 const uint8_t *value,uint64_t size,unsigned flags);
int64_t tmp_getxattr(const struct nas_tmp_file *file,const char *name,
                     uint8_t *value,uint64_t capacity);
int64_t tmp_listxattr(const struct nas_tmp_file *file,
                      uint8_t *names,uint64_t capacity);
int tmp_removexattr(struct nas_tmp_file *file,const char *name);
/* Returns 1 for an allocated logical extent, 0 at the end, negative errno.
 * RAM physical frames are NEVER exposed as on-disk physical offsets. */
int tmp_fiemap_next(const struct nas_tmp_file *file,uint64_t start,
                    uint64_t end,uint64_t *logical,uint64_t *length,
                    bool *last);
#endif
