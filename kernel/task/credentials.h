#ifndef NASEED_TASK_CREDENTIALS_H
#define NASEED_TASK_CREDENTIALS_H
#include <stdint.h>
#include "../errno.h"

/* The current kernel has precisely one real credential identity: uid/gid 0
 * and an empty supplementary-group set. Do not claim to switch identities
 * before a process credential owner and permission checks exist.
 * These successful operations are genuine no-ops against that state. */
static inline uint64_t nas_credentials_set_id(uint64_t requested) {
    return requested==0 ? 0 : (uint64_t)-EPERM;
}
static inline uint64_t nas_credentials_setgroups(uint64_t count) {
    return count==0 ? 0 : (uint64_t)-EPERM;
}
static inline uint64_t nas_credentials_getgroups(uint64_t count) {
    (void)count;
    return 0; /* no supplementary group IDs to copy */
}
#endif
