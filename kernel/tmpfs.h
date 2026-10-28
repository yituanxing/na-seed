#ifndef NASEED_TMPFS_H
#define NASEED_TMPFS_H
#include <stdbool.h>
#include <stdint.h>
/* Volatile, finite /tmp backend. This is not yet a generic Linux VFS.
 * Transition seam: syscall/user memory remains with m1.c; namespace and
 * inode/open-description lifetimes belong here. */
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

struct nas_tmp_file {
    bool used, is_dir;
    uint32_t nlink; /* directory-entry references, not open FD references */
    uint64_t ino; /* stable object identity, never derived from a pathname */
    uint64_t index_root; /* physical address of optional 512-entry root frame */
    uint64_t size;
    uint64_t atime_ns, mtime_ns, ctime_ns; /* UNIX realtime nanoseconds */
    uint32_t mode;
 };

bool tmp_path(const char *path);
bool tmp_parent(const char *path, char parent[256]);
/* A dentry names an inode. Multiple dentries may reference one regular file.
 * Only namespace code owns and mutates path strings. */
struct nas_tmp_dentry {
    bool used;
    char path[256];
    struct nas_tmp_file *inode;
};
const char *tmp_name(const struct nas_tmp_file *file);
const struct nas_tmp_dentry *tmp_child_entry(const char *parent,unsigned index);
struct nas_tmp_file *tmp_inode_alloc(bool dir,uint32_t mode);
void tmp_inode_link(struct nas_tmp_file *file);
void tmp_inode_unlink(struct nas_tmp_file *file);
void tmp_inode_discard(struct nas_tmp_file *file);
void tmp_inode_touch(struct nas_tmp_file *file);
int tmp_link(const char *oldpath,const char *newpath);
struct nas_tmp_file *tmp_find(const char *path);
/* Provider-local directory child lookup; the generic VFS passes only an
 * opaque parent inode and one component, never an absolute pathname. */
struct nas_tmp_file *tmp_find_child(const void *parent_inode,const char *name);
int tmp_parent_check(const char *path);
struct nas_tmp_file *tmp_create(const char *path, bool dir,uint32_t mode);
bool tmp_is_link(const struct nas_tmp_file *file);
int tmp_symlink(const char *target, const char *path);
int tmp_link_target(const struct nas_tmp_file *file, char target[256]);
uint64_t tmp_number(const struct nas_tmp_file *file);

/* Slot identity is shared across fork/dup; the caller owns the descriptor
 * refcount and calls tmp_release_slot only after its final decrement. */
void tmp_bind_slot(unsigned slot, const struct nas_tmp_file *file);
struct nas_tmp_file *tmp_by_slot(unsigned slot);
void tmp_release_slot(unsigned slot);
/* Kernel-buffer I/O, shared across Linux FD syscalls and ELF image reader.
 * Read of sparse holes returns zero; write allocates pages only on demand.
 * Negative errno only if no bytes were written. */
int64_t tmp_read_at(const struct nas_tmp_file *file, uint64_t offset,
                    uint8_t *dst, uint64_t count);
int64_t tmp_write_at(struct nas_tmp_file *file, uint64_t offset,
                     const uint8_t *src, uint64_t count);
void tmp_truncate(struct nas_tmp_file *file);
int tmp_resize(struct nas_tmp_file *file,uint64_t size);
int tmp_utimens(struct nas_tmp_file *file, uint64_t access_ns,
                uint64_t modified_ns, bool set_access, bool set_modified);

int tmp_chmod(struct nas_tmp_file *file, uint64_t mode);
int tmp_mkdir(const char *path, uint64_t mode);
int tmp_unlink(const char *path, uint64_t flags);
int tmp_rename(const char *oldpath, const char *newpath, uint64_t flags);
#endif
