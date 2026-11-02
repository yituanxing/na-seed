#ifndef NASEED_FS_OPEN_PATH_H
#define NASEED_FS_OPEN_PATH_H
#include <stdint.h>
#include <stdbool.h>
struct nas_fs_path_request;
/* Temporary bootstrap fallback is limited to real unmounted Limine
 * modules and explicitly synthesized devices; ordinary ENOENT never
 * retries through the old pathname adapter. */
bool nas_fs_bootstrap_endpoint(const char *absolute);
bool nas_fs_bootstrap_candidate(const struct nas_fs_path_request *request,
                                 char candidate[256]);
/* Internal open helper for the allowed bootstrap compatibility endpoints. */
uint64_t nas_fs_open_path(const char *path, uint64_t flags, uint64_t mode);
/* Linux memfd_create: reserve one FD, create and pin an unlinked tmpfs
 * inode via VFS, then publish the same regular FileOps as normal openat.
 * user_name is a kernel-owned diagnostic label, never a pathname. */
uint64_t nas_fs_open_anon_tmpfs(const char *user_name,bool cloexec);
#endif
