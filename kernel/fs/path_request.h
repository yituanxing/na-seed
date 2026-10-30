#ifndef NASEED_FS_PATH_REQUEST_H
#define NASEED_FS_PATH_REQUEST_H
#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"

/* A Linux pathname and its independently owned lookup anchors. The syscall
 * layer copies userspace once, then hands these stable identities to namei.
 * A relative lookup starts from the original cwd/open File, never its old
 * spelling. Absolute lookups ignore dirfd, as Linux requires. The 256-byte
 * copy limit is interim until namei receives a non-stack PATH_MAX workspace.
 */
struct nas_fs_path_request {
    char pathname[256];
    struct nas_vfs_path start;
    struct nas_vfs_path root;
};
struct nas_fs_node;
int nas_fs_user_path_begin(uint64_t dirfd,uint64_t user_pointer,
                           struct nas_fs_path_request *request);
int nas_fs_kernel_path_begin(uint64_t dirfd,const char *kernel_path,
                             struct nas_fs_path_request *request);
void nas_fs_path_end(struct nas_fs_path_request *request);
/* Acquires one positive Dentry ref and one request (root/start) reference;
 * callers must release both after using the borrowed inode snapshot. */
int nas_fs_user_lookup_begin(uint64_t dirfd,uint64_t user_pointer,
                             bool follow_final,
                             struct nas_fs_path_request *request,
                             struct nas_vfs_path *found,
                             struct nas_fs_node *node);
#endif
