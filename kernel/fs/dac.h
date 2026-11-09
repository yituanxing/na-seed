#ifndef NASEED_FS_DAC_H
#define NASEED_FS_DAC_H
#include "lookup.h"
#include "../task.h"
#include "../errno.h"
/* M253 candidate: nonroot fsuid/fsgid DAC; caller must disable identity
 * transitions until namei, all metadata and broker paths are audited.
 * Kernel-internal bootstrap (no Task) and uid 0 preserve legacy behavior. */
#define NAS_DAC_READ 4U
#define NAS_DAC_WRITE 2U
#define NAS_DAC_SEARCH 1U
static inline bool nas_dac_nonroot(void) {
    return nas_current_task &&
        (nas_current_task->credentials.fsuid!=0 ||
         nas_current_task->credentials.euid!=0);
}
static inline bool nas_dac_member_of(uint32_t gid) {
    if(!nas_current_task)return true;
    const struct nas_credentials *cred=&nas_current_task->credentials;
    if(cred->fsgid==gid)return true;
    for(uint32_t i=0;i<cred->group_count;i++)
        if(cred->groups[i]==gid)return true;
    return false;
}
static inline int nas_dac_mode(const struct nas_fs_node *node,unsigned mask) {
    if(!nas_dac_nonroot())return 0;
    /* A nonroot effective identity must never borrow a stale fsuid=0
     * privilege after capability teardown. Treat mismatched root FSID as
     * inaccessible until credential transitions are fully proven. */
    if(nas_current_task->credentials.fsuid==0)return -EACCES;
    struct nas_fs_node fresh;
    int rc=nas_vfs_getattr(node,&fresh);
    if(rc)return rc;
    uint32_t uid=nas_current_task->credentials.fsuid;
    unsigned bits=(fresh.uid==uid) ? ((fresh.mode>>6)&7U) :
        (nas_dac_member_of(fresh.gid) ? ((fresh.mode>>3)&7U) : (fresh.mode&7U));
    return (bits&mask)==mask ? 0 : -EACCES;
}
static inline int nas_dac_owner(const struct nas_fs_node *node) {
    if(!nas_dac_nonroot())return 0;
    if(nas_current_task->credentials.fsuid==0)return -EPERM;
    struct nas_fs_node fresh;
    int rc=nas_vfs_getattr(node,&fresh);
    if(rc)return rc;
    return fresh.uid==nas_current_task->credentials.fsuid ? 0 : -EPERM;
}
static inline int nas_dac_sticky(const struct nas_fs_node *parent,
                                 const struct nas_fs_node *victim) {
    if(!nas_dac_nonroot())return 0;
    if(nas_current_task->credentials.fsuid==0)return -EPERM;
    struct nas_fs_node p,v;
    int rc=nas_vfs_getattr(parent,&p);
    if(rc)return rc;
    if(!(p.mode&01000U))return 0;
    rc=nas_vfs_getattr(victim,&v);
    if(rc)return rc;
    uint32_t uid=nas_current_task->credentials.fsuid;
    return uid==p.uid || uid==v.uid ? 0 : -EPERM;
}
#endif
