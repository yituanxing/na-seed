/* Genuine kernel-owned SysV IPC identifiers and Linux LP64 metadata ABI.
 * Allocation reserves a typed IPC object; it does not pretend shared
 * attachments, semaphore operations or messages are already supported. */
#include "sysv.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "../time/clock.h"
#include "../arch/arch.h"
#include "../lib/string.h"
#include <stddef.h>

#define IPC_MAX_OBJECTS 8U
#define IPC_PRIVATE 0
#define IPC_CREAT 01000U
#define IPC_EXCL 02000U
#define IPC_RMID 0U
#define IPC_SET 1U
#define IPC_STAT 2U
#define IPC_64 0x100U

struct ipc_perm_lp64 {
    int32_t key;
    uint32_t uid,gid,cuid,cgid,mode;
    uint16_t seq,pad2;
    uint64_t unused1,unused2;
};
struct shmid_ds_lp64 {
    struct ipc_perm_lp64 perm;
    uint64_t size,atime,dtime,ctime;
    int32_t cpid,lpid;
    uint64_t nattch,unused4,unused5;
};
struct semid_ds_lp64 {
    struct ipc_perm_lp64 perm;
    uint64_t otime,ctime,nsems,unused3,unused4;
};
struct msqid_ds_lp64 {
    struct ipc_perm_lp64 perm;
    uint64_t stime,rtime,ctime,cbytes,qnum,qbytes;
    int32_t lspid,lrpid;
    uint64_t unused4,unused5;
};
_Static_assert(sizeof(struct ipc_perm_lp64)==48,"Linux LP64 ipc64_perm");
_Static_assert(sizeof(struct shmid_ds_lp64)==112 &&
               offsetof(struct shmid_ds_lp64,size)==48,
               "Linux LP64 shmid64_ds");
_Static_assert(sizeof(struct semid_ds_lp64)==88 &&
               offsetof(struct semid_ds_lp64,nsems)==64,
               "Linux LP64 semid64_ds (musl sem_nsems low 16)");
_Static_assert(sizeof(struct msqid_ds_lp64)==120 &&
               offsetof(struct msqid_ds_lp64,qbytes)==88,
               "Linux LP64 msqid64_ds");

struct ipc_object {
    bool used;
    uint32_t id;
    int32_t key;
    uint32_t mode,creator_pid;
    uint64_t count,ctime;
};
static struct ipc_object shared[IPC_MAX_OBJECTS],
                         semaphores[IPC_MAX_OBJECTS],
                         queues[IPC_MAX_OBJECTS];
static uint32_t next_id[3]={1,1,1};

static struct ipc_object *table(enum nas_sysvipc_kind kind) {
    if(kind==NAS_SYSVIPC_SHM)return shared;
    if(kind==NAS_SYSVIPC_SEM)return semaphores;
    if(kind==NAS_SYSVIPC_MSG)return queues;
    return 0;
}
static struct ipc_object *by_id(struct ipc_object *items,uint64_t id) {
    if(id>INT32_MAX)return 0;
    for(unsigned i=0;i<IPC_MAX_OBJECTS;i++)
        if(items[i].used && items[i].id==id)return &items[i];
    return 0;
}
static uint64_t now_seconds(void) {
    uint64_t realtime;
    return nas_time_realtime_ns(&realtime) ? realtime/1000000000ULL : 0;
}
static void metadata(const struct ipc_object *item,struct ipc_perm_lp64 *out) {
    *out=(struct ipc_perm_lp64){.key=item->key,.mode=item->mode};
}

bool nas_sysvipc_snapshot(enum nas_sysvipc_kind kind,unsigned ordinal,
                         struct nas_sysvipc_snapshot *out) {
    struct ipc_object *items=table(kind);
    if(!items || !out)return false;
    unsigned found=0;
    for(unsigned i=0;i<IPC_MAX_OBJECTS;i++) {
        const struct ipc_object *p=&items[i];
        if(!p->used)continue;
        if(found++!=ordinal)continue;
        *out=(struct nas_sysvipc_snapshot){
            .id=p->id,.key=p->key,.mode=p->mode,.creator_pid=p->creator_pid,
            .count=p->count,.ctime=p->ctime
        };
        return true;
    }
    return false;
}
static uint64_t allocate(enum nas_sysvipc_kind kind,uint64_t key,
                         uint64_t count,uint64_t flags) {
    struct ipc_object *items=table(kind);
    if(!items)return (uint64_t)-EINVAL;
    int32_t signed_key=(int32_t)key;
    if(kind==NAS_SYSVIPC_SHM && (!count || count>UINT32_MAX))
        return (uint64_t)-EINVAL;
    if(kind==NAS_SYSVIPC_SEM && (!count || count>256))
        return (uint64_t)-EINVAL;
    if(key && (uint64_t)(int64_t)signed_key!=key && (uint64_t)(uint32_t)signed_key!=key)
        return (uint64_t)-EINVAL;
    if(signed_key!=IPC_PRIVATE)for(unsigned i=0;i<IPC_MAX_OBJECTS;i++) {
        struct ipc_object *p=&items[i];
        if(!p->used || p->key!=signed_key)continue;
        if((flags & (IPC_CREAT|IPC_EXCL))==(IPC_CREAT|IPC_EXCL))
            return (uint64_t)-EEXIST;
        if(kind==NAS_SYSVIPC_SHM && count>p->count)
            return (uint64_t)-EINVAL;
        if(kind==NAS_SYSVIPC_SEM && count>p->count)
            return (uint64_t)-EINVAL;
        return p->id;
    }
    if(signed_key!=IPC_PRIVATE && !(flags&IPC_CREAT))
        return (uint64_t)-ENOENT;
    for(unsigned i=0;i<IPC_MAX_OBJECTS;i++) {
        struct ipc_object *p=&items[i];
        if(p->used)continue;
        uint32_t *counter=&next_id[(unsigned)kind-1U];
        if(*counter>INT32_MAX)return (uint64_t)-ENOSPC;
        *p=(struct ipc_object){.used=true,.id=(*counter)++, .key=signed_key,
                               .mode=(uint32_t)(flags&0777U),
                               .creator_pid=(uint32_t)nas_current_task->tgid,
                               .count=kind==NAS_SYSVIPC_MSG?16384U:count,
                               .ctime=now_seconds()};
        return p->id;
    }
    return (uint64_t)-ENOSPC;
}
static uint64_t control(enum nas_sysvipc_kind kind,uint64_t id,uint64_t cmd,
                        uint64_t buffer) {
    struct ipc_object *p=by_id(table(kind),id);
    if(!p)return (uint64_t)-EINVAL;
    unsigned operation=(unsigned)cmd & ~IPC_64;
    if((cmd & ~(uint64_t)(IPC_64|0xffU)))return (uint64_t)-EINVAL;
    if(operation==IPC_RMID) {*p=(struct ipc_object){0};return 0;}
    if(operation==IPC_SET)return (uint64_t)-EOPNOTSUPP;
    if(operation!=IPC_STAT)return (uint64_t)-EINVAL;
    if(!buffer)return (uint64_t)-EFAULT;
    if(kind==NAS_SYSVIPC_SHM) {
        struct shmid_ds_lp64 ds={0};
        metadata(p,&ds.perm);
        ds.size=p->count;ds.ctime=p->ctime;
        ds.cpid=(int32_t)p->creator_pid;
        return nas_user_write_bytes(buffer,(const uint8_t *)&ds,sizeof(ds)) ?
               0 : (uint64_t)-EFAULT;
    }
    if(kind==NAS_SYSVIPC_SEM) {
        struct semid_ds_lp64 ds={0};
        metadata(p,&ds.perm);
        ds.nsems=p->count;ds.ctime=p->ctime;
        return nas_user_write_bytes(buffer,(const uint8_t *)&ds,sizeof(ds)) ?
               0 : (uint64_t)-EFAULT;
    }
    struct msqid_ds_lp64 ds={0};
    metadata(p,&ds.perm);
    ds.qbytes=p->count;ds.ctime=p->ctime;
    return nas_user_write_bytes(buffer,(const uint8_t *)&ds,sizeof(ds)) ?
           0 : (uint64_t)-EFAULT;
}

/* operation: 0=get, 1=ctl, 2=attach/send/op, 3=detach/receive.
 * Linux syscalls dispatch only supported typed object operations here. */
uint64_t nas_sysvipc_call(enum nas_sysvipc_kind kind,unsigned operation,
                         uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    if(!table(kind))return (uint64_t)-EINVAL;
    if(operation==0) {
        if(kind==NAS_SYSVIPC_MSG)return allocate(kind,a,0,b);
        return allocate(kind,a,b,c);
    }
    if(operation==1) {
        if(kind==NAS_SYSVIPC_SEM)return control(kind,a,c,d);
        return control(kind,a,b,c);
    }
    return (uint64_t)-ENOSYS;
}
