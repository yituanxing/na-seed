#ifndef NASEED_TASK_CREDENTIALS_H
#define NASEED_TASK_CREDENTIALS_H
#include <stdbool.h>
#include <stdint.h>
#include "../errno.h"

/* Zero-initialized tasks preserve the historical uid/gid 0 baseline.
 * M253 optionally enables real/saved/effective/filesystem primary IDs only
 * when compile-time full VFS DAC enforcement is explicitly acknowledged.
 * Supplementary group state is always stored in each Task. */
#define NAS_CREDENTIAL_GROUP_LIMIT 64U
struct nas_credentials {
    /* Primary IDs are zero-initialized root, including legacy CI. */
    uint32_t ruid,euid,suid,fsuid;
    uint32_t rgid,egid,sgid,fsgid;
    uint32_t groups[NAS_CREDENTIAL_GROUP_LIMIT];
    uint32_t group_count;
    /* Linux capability state is per task. Legacy zero-initialized root tasks
     * have the two implemented privileges NET_RAW and NET_ADMIN (the latter
     * only for the bounded nf_tables control plane); cap_initialized
     * distinguishes that default from an explicit irreversible drop. */
    uint64_t cap_effective,cap_permitted;
    bool cap_initialized;
    /* Linux PR_SET_KEEPCAPS is inherited by fork and cleared by execve.
     * Identity transitions are gated on full VFS DAC enforcement. */
    bool cap_keep_on_setuid;
};

/* Full VFS DAC, signal/ptrace and capability gates are mandatory before any
 * nonzero primary identity can be represented.  Incomplete builds fail closed. */
#if defined(NAS_ENABLE_M253_NONROOT_DAC) && !defined(NAS_M253_DAC_ENFORCEMENT_COMPLETE)
#error "M253 cannot activate nonroot identity without complete VFS DAC"
#endif
#ifdef NAS_ENABLE_M253_NONROOT_DAC
static inline bool nas_cred_allowed(uint32_t id,uint32_t r,uint32_t e,uint32_t s,bool privileged) {
    return id==UINT32_MAX || privileged || id==r || id==e || id==s;
}
static inline void nas_cred_effective_uid(struct nas_credentials *c,uint32_t id) {
    if(c->euid==0 && id!=0) {
        if(!c->cap_initialized) {
            /* Same two bounded capabilities as kernel/task/capability.h. */
            c->cap_permitted=(UINT64_C(1)<<12)|(UINT64_C(1)<<13);
            c->cap_effective=c->cap_permitted;
        }
        c->cap_initialized=true;
        c->cap_effective=0;
        if(!c->cap_keep_on_setuid)c->cap_permitted=0;
    } else if(c->euid!=0 && id==0) {
        c->cap_initialized=true;
        c->cap_effective=c->cap_permitted;
    }
    c->euid=c->fsuid=id;
}
static inline uint64_t nas_cred_setuid(struct nas_credentials *c,uint64_t requested) {
    uint32_t id=(uint32_t)requested;
    if(c->euid==0) {
        nas_cred_effective_uid(c,id);
        c->ruid=c->suid=id;
    } else {
        if(id!=c->ruid && id!=c->suid)return (uint64_t)-EPERM;
        nas_cred_effective_uid(c,id);
    }
    return 0;
}
static inline uint64_t nas_cred_setgid(struct nas_credentials *c,uint64_t requested) {
    uint32_t id=(uint32_t)requested;
    if(c->euid==0)c->rgid=c->sgid=id;
    else if(id!=c->rgid && id!=c->sgid)return (uint64_t)-EPERM;
    c->egid=c->fsgid=id;
    return 0;
}
static inline uint64_t nas_cred_setresuid(struct nas_credentials *c,
                                           uint64_t real,uint64_t effective,uint64_t saved) {
    uint32_t r=(uint32_t)real,e=(uint32_t)effective,s=(uint32_t)saved;
    bool privileged=c->euid==0;
    if(!nas_cred_allowed(r,c->ruid,c->euid,c->suid,privileged) ||
       !nas_cred_allowed(e,c->ruid,c->euid,c->suid,privileged) ||
       !nas_cred_allowed(s,c->ruid,c->euid,c->suid,privileged))
        return (uint64_t)-EPERM;
    if(r!=UINT32_MAX)c->ruid=r;
    if(s!=UINT32_MAX)c->suid=s;
    if(e!=UINT32_MAX)nas_cred_effective_uid(c,e);
    return 0;
}
static inline uint64_t nas_cred_setresgid(struct nas_credentials *c,
                                           uint64_t real,uint64_t effective,uint64_t saved) {
    uint32_t r=(uint32_t)real,e=(uint32_t)effective,s=(uint32_t)saved;
    bool privileged=c->euid==0;
    if(!nas_cred_allowed(r,c->rgid,c->egid,c->sgid,privileged) ||
       !nas_cred_allowed(e,c->rgid,c->egid,c->sgid,privileged) ||
       !nas_cred_allowed(s,c->rgid,c->egid,c->sgid,privileged))
        return (uint64_t)-EPERM;
    if(r!=UINT32_MAX)c->rgid=r;
    if(s!=UINT32_MAX)c->sgid=s;
    if(e!=UINT32_MAX)c->egid=c->fsgid=e;
    return 0;
}
static inline uint64_t nas_cred_setreuid(struct nas_credentials *c,uint64_t real,uint64_t effective) {
    uint32_t r=(uint32_t)real,e=(uint32_t)effective,oldr=c->ruid;
    bool privileged=c->euid==0;
    if((r!=UINT32_MAX && !privileged && r!=c->ruid && r!=c->euid) ||
       !nas_cred_allowed(e,c->ruid,c->euid,c->suid,privileged))
        return (uint64_t)-EPERM;
    if(r!=UINT32_MAX)c->ruid=r;
    if(e!=UINT32_MAX)nas_cred_effective_uid(c,e);
    if(r!=UINT32_MAX || (e!=UINT32_MAX && e!=oldr))c->suid=c->euid;
    return 0;
}
static inline uint64_t nas_cred_setregid(struct nas_credentials *c,uint64_t real,uint64_t effective) {
    uint32_t r=(uint32_t)real,e=(uint32_t)effective,oldr=c->rgid;
    bool privileged=c->euid==0;
    if((r!=UINT32_MAX && !privileged && r!=c->rgid && r!=c->egid) ||
       !nas_cred_allowed(e,c->rgid,c->egid,c->sgid,privileged))
        return (uint64_t)-EPERM;
    if(r!=UINT32_MAX)c->rgid=r;
    if(e!=UINT32_MAX)c->egid=c->fsgid=e;
    if(r!=UINT32_MAX || (e!=UINT32_MAX && e!=oldr))c->sgid=c->egid;
    return 0;
}
static inline uint64_t nas_cred_setfsuid(struct nas_credentials *c,uint64_t requested) {
    uint32_t id=(uint32_t)requested,old=c->fsuid;
    if(c->euid==0 || id==c->fsuid ||
       id==c->ruid || id==c->euid || id==c->suid)c->fsuid=id;
    return old;
}
static inline uint64_t nas_cred_setfsgid(struct nas_credentials *c,uint64_t requested) {
    uint32_t id=(uint32_t)requested,old=c->fsgid;
    if(c->euid==0 || id==c->fsgid ||
       id==c->rgid || id==c->egid || id==c->sgid)c->fsgid=id;
    return old;
}
static inline bool nas_cred_can_signal(const struct nas_credentials *sender,
                                        const struct nas_credentials *target,
                                        bool same_session_sigcont) {
    return sender->euid==0 || same_session_sigcont ||
           sender->ruid==target->ruid || sender->ruid==target->suid ||
           sender->euid==target->ruid || sender->euid==target->suid;
}
static inline bool nas_cred_can_ptrace(const struct nas_credentials *sender) {
    /* Until dumpability and CAP_SYS_PTRACE exist, nonroot tracing is denied. */
    return sender->euid==0 && sender->fsuid==0;
}
#endif

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
#ifdef NAS_ENABLE_M253_NONROOT_DAC
    if(credentials->euid!=0)return (uint64_t)-EPERM;
#endif
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
