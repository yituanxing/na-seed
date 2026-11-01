#include <stdint.h>
#include "../lib/string.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "open_path.h"
#include "io.h"
#include "lookup.h"
#include "vfs.h"
#include "memfd.h"

#define NAS_MFD_CLOEXEC      0x0001U
#define NAS_MFD_ALLOW_SEALING 0x0002U
#define NAS_MFD_HUGETLB      0x0004U
#define NAS_MFD_NOEXEC_SEAL  0x0008U

/* A memfd is an anonymous tmpfs inode, not an open handle to a pathname.
 * Reuse the actual VFS tmpfs inode, sparse page store, regular-file FileOps,
 * close-on-exec descriptors, and live MAP_SHARED backing. The private name
 * exists only inside this non-blocking kernel syscall while an FD/inode pin
 * is acquired; unlink immediately makes the inode unreachable by name.
 * Thus ftruncate, pread/pwrite, dup/fork, mmap, and close use exactly the
 * existing inode/description lifetime instead of a competing memfd store.
 * This bounded implementation requires the ordinary /tmp tmpfs namespace. */
static uint64_t memfd_next_id;

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

    static const char prefix[]="/tmp/.naseed-memfd-";
    static const char hex[]="0123456789abcdef";
    char path[sizeof(prefix)+16];
    memcpy(path,prefix,sizeof(prefix)-1);
    path[sizeof(prefix)-1+16]=0;

    /* Internal names are unique even for equal user-supplied descriptions.
     * EEXIST skips a name that a real pathname user happened to occupy. */
    for(unsigned attempt=0;attempt<128;attempt++) {
        uint64_t id=__atomic_add_fetch(&memfd_next_id,1,__ATOMIC_RELAXED);
        for(unsigned i=0;i<16;i++)
            path[sizeof(prefix)-1+i]=hex[(id>>(60U-4U*i))&15U];

        /* O_RDWR | O_CREAT | O_EXCL, with optional O_CLOEXEC. The
         * centralized open helper performs transactional FD reservation. */
        uint64_t fd=nas_fs_open_path(path,0x2ULL|0x40ULL|0x80ULL|
                                     ((flags&NAS_MFD_CLOEXEC)?0x80000ULL:0),
                                     0777U);
        if((int64_t)fd==-EEXIST)continue;
        if((int64_t)fd<0)return fd;

        /* memfd_create is unaffected by the calling process's umask:
         * an ordinary Linux memfd starts with mode 0777. */
        struct nas_fs_node inode={0};
        rc=nas_vfs_lookup(path,&inode);
        if(!rc)rc=nas_vfs_chmod(&inode,0777U);
        if(rc) {
            (void)nas_vfs_unlink(path,0);
            nas_fs_close_fd(fd);
            return (uint64_t)(int64_t)rc;
        }

        rc=nas_vfs_unlink(path,0);
        if(rc) {
            nas_fs_close_fd(fd);
            return (uint64_t)(int64_t)rc;
        }
        return fd;
    }
    return (uint64_t)-EEXIST;
}
