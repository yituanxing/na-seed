#ifndef NASEED_TMPFS_H
#define NASEED_TMPFS_H
#include <stdbool.h>
#include <stdint.h>

/* Volatile, finite /tmp backend. This is not yet a generic Linux VFS.
 * Transition seam: syscall/user memory remains with m1.c; namespace and
 * inode/open-description lifetimes belong here. */
#define TMP_FILE_COUNT 8
/* Sparse, demand-allocated 4-KiB physical pages. Each indirect page has
 * 512 data-frame references, so 16 index slots cover 32 MiB per inode.
 * No 32-MiB kernel array is reserved when a file is created. */
#define TMP_PAGE_SIZE 4096ULL
#define TMP_PAGE_ENTRIES 512U
#define TMP_INDEX_PAGES 16U
#define TMP_FILE_CAP (TMP_PAGE_SIZE * TMP_PAGE_ENTRIES * TMP_INDEX_PAGES)

struct nas_tmp_file {
    bool used, linked, is_dir;
    char path[256];
    uint64_t index_pages[TMP_INDEX_PAGES]; /* physical index-frame addresses */
    uint64_t size;
    uint32_t mode;
};

bool tmp_path(const char *path);
bool tmp_parent(const char *path, char parent[256]);
struct nas_tmp_file *tmp_find(const char *path);
int tmp_parent_check(const char *path);
struct nas_tmp_file *tmp_create(const char *path, bool dir);
unsigned tmp_number(const struct nas_tmp_file *file);
const struct nas_tmp_file *tmp_child(const char *parent, unsigned index);

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
int tmp_chmod(struct nas_tmp_file *file, uint64_t mode);
int tmp_mkdir(const char *path, uint64_t mode);
int tmp_unlink(const char *path, uint64_t flags);
int tmp_rename(const char *oldpath, const char *newpath, uint64_t flags);
#endif
