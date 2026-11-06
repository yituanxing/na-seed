#ifndef NASEED_TASK_CREDENTIALS_H
#define NASEED_TASK_CREDENTIALS_H
#include <stdbool.h>
#include <stdint.h>
#include "../errno.h"

/* The current kernel has precisely one supported real primary credential
 * identity: uid/gid 0.  The supplementary group vector is actual Task state:
 * initgroups(root,0) from original BusyBox login can include wheel (gid 10)
 * as well as group 0.  Root's supplementary groups are tracked and reported
 * accurately; non-root primary identities require VFS permission support. */
#define NAS_CREDENTIAL_GROUP_LIMIT 64U
struct nas_credentials {
    uint32_t groups[NAS_CREDENTIAL_GROUP_LIMIT];
    uint32_t group_count;
    /* Linux capability state is per task. Legacy zero-initialized root tasks
     * have the single capability NaSeed actually enforces, CAP_NET_RAW.
     * cap_initialized distinguishes that default from an explicit drop. */
    uint64_t cap_effective,cap_permitted;
    bool cap_initialized;
    /* Linux PR_SET_KEEPCAPS is inherited by fork and cleared by execve.
     * Retaining this real task flag does not authorize non-root identities:
     * those remain forbidden until the VFS has an enforcing DAC model. */
    bool cap_keep_on_setuid;
};

static inline uint64_t nas_credentials_set_id(uint64_t requested) {
    /* Linux uid_t/gid_t are 32-bit even on LP64.  Higher syscall register
     * bits do not change the requested identity. */
    return (uint32_t)requested==0 ? 0 : (uint64_t)-EPERM;
}
/* Linux setreuid/setregid and setresuid/setresgid allow the sentinel
 * (uid_t)-1 to leave a field alone.  Neither a nonzero real, effective,
 * nor saved identity is representable until NaSeed enforces non-root DAC
 * across ALL VFS paths, metadata updates and privileged operations.
 * Reject rather than claim a security-sensitive identity transition. */
static inline uint64_t nas_credentials_set_root_ids(uint64_t real,
                                                     uint64_t effective,
                                                     uint64_t saved) {
    const uint32_t r=(uint32_t)real,e=(uint32_t)effective,s=(uint32_t)saved;
    if((r!=0 && r!=UINT32_MAX) || (e!=0 && e!=UINT32_MAX) ||
       (s!=0 && s!=UINT32_MAX))return (uint64_t)-EPERM;
    return 0;
}
/* Linux setfsuid/setfsgid return the PREVIOUS filesystem identity, not
 * a success/error result. Our only representable fs identity is root (0):
 * querying with -1 returns 0, and requests to change it leave it at 0.
 * Do not claim an unimplemented non-root permission transition. */
static inline uint64_t nas_credentials_set_fs_id(uint64_t requested) {
    (void)requested;
    return 0;
}
/* Pure checked transition: user buffer access is performed transactionally
 * by syscall dispatch BEFORE this helper can mutate Task-owned credentials.
 * Linux root may join supplementary groups with nonzero GIDs while remaining
 * the same primary root user/group.  Linux sorts the group vector. */
static inline uint64_t nas_credentials_setgroups(struct nas_credentials *credentials,
                                                  uint64_t count,
                                                  const uint32_t *groups) {
    if(count>NAS_CREDENTIAL_GROUP_LIMIT)return (uint64_t)-EINVAL;
    if(count && !groups)return (uint64_t)-EFAULT;
    for(uint64_t i=0;i<count;i++)credentials->groups[i]=groups[i];
    for(uint64_t i=1;i<count;i++) {
        uint32_t value=credentials->groups[i];
        uint64_t j=i;
        while(j && credentials->groups[j-1]>value) {
            credentials->groups[j]=credentials->groups[j-1];
            j--;
        }
        credentials->groups[j]=value;
    }
    credentials->group_count=(uint32_t)count;
    return 0;
}
static inline uint64_t nas_credentials_getgroups(const struct nas_credentials *credentials,
                                                  uint64_t count,
                                                  uint32_t *groups) {
    if(count>INT32_MAX)return (uint64_t)-EINVAL; /* Linux int size ABI */
    if(count==0)return credentials->group_count;
    if(count<credentials->group_count)return (uint64_t)-EINVAL;
    if(credentials->group_count && !groups)return (uint64_t)-EFAULT;
    for(uint32_t i=0;i<credentials->group_count;i++)groups[i]=credentials->groups[i];
    return credentials->group_count;
}
#endif
