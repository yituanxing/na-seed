#ifndef NASEED_TASK_CAPABILITY_H
#define NASEED_TASK_CAPABILITY_H
#include <stdint.h>
#include "credentials.h"

/* Linux UAPI linux/capability.h. All three NaSeed targets are little-endian
 * LP64, but each user field is exactly 32 bits, including pid_t. */
#define NAS_CAP_VERSION_1 UINT32_C(0x19980330)
#define NAS_CAP_VERSION_2 UINT32_C(0x20071026)
#define NAS_CAP_VERSION_3 UINT32_C(0x20080522)
#define NAS_CAP_NET_RAW (UINT64_C(1)<<13)

struct nas_cap_user_header { uint32_t version; int32_t pid; };
struct nas_cap_user_data {
    uint32_t effective, permitted, inheritable;
};
_Static_assert(sizeof(struct nas_cap_user_header)==8,"Linux cap header ABI");
_Static_assert(sizeof(struct nas_cap_user_data)==12,"Linux cap data ABI");

static inline unsigned nas_cap_version_words(uint32_t version) {
    if(version==NAS_CAP_VERSION_1)return 1;
    if(version==NAS_CAP_VERSION_2 || version==NAS_CAP_VERSION_3)return 2;
    return 0;
}

/* Linux capget(NULL data) allows probing an unknown version and returns 0
 * after writing back v3. capset and capget(with data) return EINVAL. */
static inline uint64_t nas_cap_negotiate_version(uint32_t *version,
                                                  bool set,bool null_data) {
    if(nas_cap_version_words(*version))return 0;
    *version=NAS_CAP_VERSION_3;
    return (!set && null_data) ? 0 : (uint64_t)-EINVAL;
}

/* Linux capset is restricted to this thread (0 or gettid); capget allows
 * any nonnegative thread PID, leaving existence lookup to dispatch. */
static inline uint64_t nas_cap_validate_target(bool set,int32_t pid,
                                                uint32_t tid) {
    if(set)return (pid && (uint32_t)pid!=tid) ? (uint64_t)-EPERM : 0;
    return pid<0 ? (uint64_t)-EINVAL : 0;
}

/* Root is NaSeed's only representable primary UID. Do NOT expose Linux's
 * all-capabilities root mask: only CAP_NET_RAW maps to implemented raw socket
 * access. The mask is task-owned and may be dropped irreversibly from P. */
static inline uint64_t nas_cap_permitted(const struct nas_credentials *cred) {
    return cred->cap_initialized ? cred->cap_permitted : NAS_CAP_NET_RAW;
}
static inline uint64_t nas_cap_effective(const struct nas_credentials *cred) {
    return cred->cap_initialized ? cred->cap_effective : NAS_CAP_NET_RAW;
}
static inline bool nas_credentials_can_net_raw(const struct nas_credentials *cred) {
    return (nas_cap_effective(cred)&NAS_CAP_NET_RAW)!=0;
}

static inline void nas_cap_get(const struct nas_credentials *cred,
                               struct nas_cap_user_data data[2]) {
    const uint64_t effective=nas_cap_effective(cred);
    const uint64_t permitted=nas_cap_permitted(cred);
    for(unsigned i=0;i<2;i++) {
        data[i].effective=(uint32_t)(effective>>(i*32));
        data[i].permitted=(uint32_t)(permitted>>(i*32));
        data[i].inheritable=0; /* no file-capability inheritance machinery */
    }
}

/* A real transition, not an unconditional root success stub. Without VFS
 * file capabilities / ambient or bounding-set support, no task may gain a
 * capability outside its previous P, and no inheritable bit may be raised.
 * Linux also requires E to be a subset of new P. */
static inline uint64_t nas_cap_set(struct nas_credentials *cred,
                                   unsigned words,
                                   const struct nas_cap_user_data data[2]) {
    if(words!=1 && words!=2)return (uint64_t)-EINVAL;
    uint64_t effective=data[0].effective;
    uint64_t permitted=data[0].permitted;
    uint64_t inheritable=data[0].inheritable;
    if(words==2) {
        effective|=(uint64_t)data[1].effective<<32;
        permitted|=(uint64_t)data[1].permitted<<32;
        inheritable|=(uint64_t)data[1].inheritable<<32;
    }
    if((effective&~permitted) || (permitted&~nas_cap_permitted(cred)) ||
       (effective&~NAS_CAP_NET_RAW) || inheritable)
        return (uint64_t)-EPERM;
    cred->cap_permitted=permitted;
    cred->cap_effective=effective;
    cred->cap_initialized=true;
    return 0;
}
#endif
