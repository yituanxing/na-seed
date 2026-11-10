/* Focused host probe for the kernel-owned M141 SysV IPC object namespace.
 * No QEMU/Alpine APK needed while accumulating the next grouped run. */
#include "../kernel/ipc/sysv.h"
#include "../kernel/task.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static struct nas_task test_task={.pid=41,.tgid=41};
struct nas_task *nas_current_task=&test_task;
bool nas_time_realtime_ns(uint64_t *value) {
    *value=1740000000000000000ULL;return true;
}
bool nas_user_write_bytes(uint64_t user,const uint8_t *value,uint64_t size) {
    if(!user)return false;
    memcpy((void *)(uintptr_t)user,value,(size_t)size);return true;
}
bool nas_user_read_bytes(uint64_t user,uint8_t *value,uint64_t size) {
    if(!user)return false;
    memcpy(value,(const void *)(uintptr_t)user,(size_t)size);return true;
}

int main(void) {
    struct nas_sysvipc_snapshot entry;
    uint8_t stat[128]={0};
    uint64_t size;
    uint64_t shmid=nas_sysvipc_call(NAS_SYSVIPC_SHM,0,0,4096,0600,0);
    assert((int64_t)shmid>0);
    assert(nas_sysvipc_snapshot(NAS_SYSVIPC_SHM,0,&entry));
    assert(entry.id==shmid && entry.creator_pid==41 && entry.count==4096);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SHM,1,shmid,2,(uint64_t)(uintptr_t)stat,0)==0);
    memcpy(&size,stat+48,sizeof(size));
    assert(size==4096);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SHM,1,shmid,0,0,0)==0);
    assert(!nas_sysvipc_snapshot(NAS_SYSVIPC_SHM,0,&entry));

    uint64_t semid=nas_sysvipc_call(NAS_SYSVIPC_SEM,0,0,2,0660,0);
    assert((int64_t)semid>0);
    memset(stat,0,sizeof(stat));
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,2,(uint64_t)(uintptr_t)stat)==0);
    memcpy(&size,stat+64,sizeof(size));
    assert(size==2);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,12,0)==0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,1,16,7)==0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,1,12,0)==7);
    uint16_t values[2]={0};
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,13,(uint64_t)(uintptr_t)values)==0);
    assert(values[0]==0 && values[1]==7);
    values[0]=23;values[1]=42;
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,17,(uint64_t)(uintptr_t)values)==0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,12,0)==23);
    assert((int64_t)nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,2,12,0)<0);
    assert((int64_t)nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,16,32768)<0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SEM,1,semid,0,0,0)==0);

    uint64_t msgid=nas_sysvipc_call(NAS_SYSVIPC_MSG,0,0,0600,0,0);
    assert((int64_t)msgid>0);
    memset(stat,0,sizeof(stat));
    assert(nas_sysvipc_call(NAS_SYSVIPC_MSG,1,msgid,2,(uint64_t)(uintptr_t)stat,0)==0);
    memcpy(&size,stat+88,sizeof(size));
    assert(size==16384);
    assert(nas_sysvipc_call(NAS_SYSVIPC_MSG,1,msgid,0,0,0)==0);

    uint64_t named=nas_sysvipc_call(NAS_SYSVIPC_SHM,0,0x1234,8192,01000|0644,0);
    assert((int64_t)named>0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SHM,0,0x1234,4096,0,0)==named);
    assert((int64_t)nas_sysvipc_call(NAS_SYSVIPC_SHM,0,0x1234,4096,03000,0)<0);
    assert(nas_sysvipc_call(NAS_SYSVIPC_SHM,1,named,0,0,0)==0);
    return 0;
}
