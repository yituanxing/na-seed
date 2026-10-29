#ifndef NASEED_FS_LOOKUP_H
#define NASEED_FS_LOOKUP_H
#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"
/* Borrowed inode view. No concrete bootfs/tmpfs/procfs variants or pointers
 * leak into common pathwalk, ELF loader or future ext4 integration. */
struct nas_fs_node {
    const struct nas_vfs_type *filesystem;
    const struct nas_vfs_mount *mount; /* origin mount, borrowed until pinned */
    /* Mount incarnation, not just its reused descriptor pointer. A stale
     * pathname/FD snapshot must not target a subsequently remounted disk. */
    uint64_t mount_id;
    const void *inode;
    /* Filesystem-supplied stable inode/stat identity. VFS never exports
     * a provider pointer as st_ino. Zero means metadata unavailable. */
    uint64_t ino;
    uint64_t nlink;
    uint32_t uid,gid;
    uint64_t atime_sec,mtime_sec,ctime_sec;
    uint32_t atime_nsec,mtime_nsec,ctime_nsec; /* provider-owned precision */
    const uint8_t *data;  /* optional zero-copy immutable data */
    uint64_t size;
    uint32_t mode;
    bool is_dir, is_symlink;
};
int nas_fs_lookup_parent(const char *absolute,struct nas_fs_node *parent,
                         char canonical[256]);
int nas_fs_canonical_candidate(const char *absolute,char canonical[256]);
/* Follow-final negative dentry for O_CREAT, never unlink/rename. */
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]);
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]);
/* Slash-only spelling names the already-existing root. Shared by syscall
 * and compatibility namespace entrypoints; it has no final child name. */
bool nas_fs_root_only(const char *path);
/* Internal path resolution never round-trips an owned cwd/dirfd into text. */
int nas_fs_lookup_path_at(const struct nas_vfs_path *start,
                          const struct nas_vfs_path *root_clamp,
                          const char *path,bool follow_final,
                          struct nas_vfs_path *out);
int nas_fs_lookup_parent_at(const struct nas_vfs_path *start,
                            const struct nas_vfs_path *root_clamp,
                            const char *path,struct nas_vfs_path *parent,
                            char leaf[256]);
enum nas_fs_parent_trailing_policy {
    NAS_FS_TRAILING_STRICT=0,      /* unlink/rename source: must name a dir */
    NAS_FS_TRAILING_MKDIR,         /* existing any kind or missing: defer to mkdir */
    NAS_FS_TRAILING_CREATE_DEST,   /* link/symlink: existing=EEXIST, missing=ENOENT */
    NAS_FS_TRAILING_RENAME_DEST    /* missing/non-directory => ENOTDIR */
};
int nas_fs_lookup_parent_at_policy(const struct nas_vfs_path *start,
                                   const struct nas_vfs_path *root_clamp,
                                   const char *path,
                                   enum nas_fs_parent_trailing_policy policy,
                                   struct nas_vfs_path *parent,
                                   char leaf[256]);
/* mkdir -p may create a missing final directory named with trailing '/'. */
int nas_fs_lookup_parent_mkdir_at(const struct nas_vfs_path *start,
                                  const struct nas_vfs_path *root_clamp,
                                  const char *path,struct nas_vfs_path *parent,
                                  char leaf[256]);
int nas_fs_resolve_create_parent_at(const struct nas_vfs_path *start,
                                    const struct nas_vfs_path *root_clamp,
                                    const char *path,
                                    struct nas_vfs_path *parent,
                                    char leaf[256]);
#endif
