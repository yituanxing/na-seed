#ifndef NASEED_FS_OPEN_PATH_H
#define NASEED_FS_OPEN_PATH_H
#include <stdint.h>
#include <stdbool.h>
struct nas_fs_path_request;
struct limine_file;
struct nas_chrdev_provider;
struct nas_chrdev_node;
/* The small rootless-boot namespace is NOT a second VFS. Describe a
 * literal, whitelisted endpoint only after Path-first namei returned
 * ENOENT. This read-only descriptor is shared by open/access/stat, so
 * device identity cannot depend on an incidental process FD number. */
struct nas_fs_bootstrap_info {
    uint8_t fd_kind;
    int pty_number;
    const struct limine_file *boot_file;
    const struct nas_chrdev_provider *chrdev_provider;
    struct nas_chrdev_node *chrdev_node;
    unsigned chrdev_unit;
    uint64_t dev,ino,rdev,mode,size;
};
int nas_fs_bootstrap_describe(const char *absolute,
                              struct nas_fs_bootstrap_info *out);
uint64_t nas_fs_bootstrap_module_ino(const struct limine_file *file);
/* Temporary bootstrap fallback is limited to real unmounted Limine
 * modules and explicitly synthesized devices; ordinary ENOENT never
 * retries through the old pathname adapter. */
bool nas_fs_bootstrap_endpoint(const char *absolute);
bool nas_fs_bootstrap_candidate(const struct nas_fs_path_request *request,
                                 char candidate[256]);
/* A failed follow-final lookup does not prove a name is absent: a real
 * dangling symlink must not turn into a synthetic bootstrap endpoint.
 * Returns the actual nofollow namei status (0 if an inode exists). */
int nas_fs_bootstrap_nofollow_probe(const struct nas_fs_path_request *request,
                                    const char *path);
/* Internal open helper for the allowed bootstrap compatibility endpoints. */
uint64_t nas_fs_open_path(const char *path, uint64_t flags, uint64_t mode);
/* Linux memfd_create: reserve one FD, create and pin an unlinked tmpfs
 * inode via VFS, then publish the same regular FileOps as normal openat.
 * user_name is a kernel-owned diagnostic label, never a pathname. */
uint64_t nas_fs_open_anon_tmpfs(const char *user_name,bool cloexec);
#endif
