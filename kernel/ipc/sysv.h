#ifndef NASEED_IPC_SYSV_H
#define NASEED_IPC_SYSV_H
#include <stdbool.h>
#include <stdint.h>

/* Linux SysV IPC has separate namespaces for shared-memory segments,
 * semaphore arrays and message queues. The current kernel implements
 * allocation, metadata inspection and removal; operations that would
 * modify message/semaphore payloads or attach shared memory stay ENOSYS. */
enum nas_sysvipc_kind {NAS_SYSVIPC_SHM=1,NAS_SYSVIPC_SEM=2,NAS_SYSVIPC_MSG=3};
struct nas_sysvipc_snapshot {
    uint32_t id;
    int32_t key;
    uint32_t mode,uid,gid,cuid,cgid,creator_pid,last_pid;
    uint64_t count,ctime,atime,dtime;
};
bool nas_sysvipc_snapshot(enum nas_sysvipc_kind kind,unsigned ordinal,
                         struct nas_sysvipc_snapshot *out);
uint64_t nas_sysvipc_call(enum nas_sysvipc_kind kind,unsigned operation,
                         uint64_t a,uint64_t b,uint64_t c,uint64_t d);
#endif
