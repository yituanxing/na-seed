#ifndef NASEED_TMPFS_H
#define NASEED_TMPFS_H
#include <stdbool.h>
#include <stdint.h>

/* Volatile, finite /tmp backend. This is not yet a generic Linux VFS.
 * Transition seam: syscall/user memory remains with m1.c; namespace and
 * inode/open-description lifetimes belong here. */
#define TMP_FILE_COUNT 8
#define TMP_FILE_CAP 16384U

struct nas_tmp_file {
    bool used, linked, is_dir;
    char path[256];
    uint8_t bytes[TMP_FILE_CAP];
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

int tmp_mkdir(const char *path, uint64_t mode);
int tmp_unlink(const char *path, uint64_t flags);
int tmp_rename(const char *oldpath, const char *newpath, uint64_t flags);
#endif
