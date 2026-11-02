#include <stdint.h>
#include "../errno.h"
#include "../mm/usercopy.h"
#include "open_path.h"
#include "memfd.h"

#define NAS_MFD_CLOEXEC      0x0001U
#define NAS_MFD_ALLOW_SEALING 0x0002U
#define NAS_MFD_HUGETLB      0x0004U
#define NAS_MFD_NOEXEC_SEAL  0x0008U

/* An unnamed inode is created directly by the existing mounted tmpfs
 * provider, pinned through the VFS lease and published as the SAME regular
 * OpenFile used by openat/fork/dup/mmap. The user name is diagnostic only:
 * it must never become a path component or pass through tmpfs namespace.
 * This implementation still needs a live tmpfs mount, not a private shmem
 * superblock; MFD_ALLOW_SEALING and MFD_HUGETLB are not implemented. */

uint64_t nas_fs_memfd_create(uint64_t user_name,uint64_t flags) {
    /* Linux kernels older than 6.3 reject MFD_NOEXEC_SEAL with EINVAL.
     * Returning EINVAL is intentional: claiming to create a noexec/sealed
     * inode without F_GET_SEALS/F_ADD_SEALS enforcement would be a lie.
     * xfsprogs correctly retries with just MFD_CLOEXEC on older kernels. */
    if(flags&NAS_MFD_NOEXEC_SEAL)return (uint64_t)-EINVAL;
    if(flags&(NAS_MFD_ALLOW_SEALING|NAS_MFD_HUGETLB))
        return (uint64_t)-EOPNOTSUPP;
    if(flags&~(uint64_t)NAS_MFD_CLOEXEC)return (uint64_t)-EINVAL;

    /* Linux memfd names can contain slashes and have at most 249 bytes;
     * these names are descriptive only, never pathname components. */
    char name[250];
    int rc=nas_user_read_cstr(user_name,name,sizeof(name));
    if(rc)return (uint64_t)(int64_t)rc;

    /* Linux memfd mode is 0777, independent of the calling umask.
     * The File creator reserves an FD before asking VFS for an inode. */
    return nas_fs_open_anon_tmpfs(name,(flags&NAS_MFD_CLOEXEC)!=0);
}
