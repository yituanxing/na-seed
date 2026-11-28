/* Kernel-owned SysV IPC objects and Linux LP64 metadata ABI.
 * SHM attaches real shared PMM frames to each process via the same
 * refcounted VMA provider used by ordinary device-backed mappings.
 * Semaphore waits and message payload operations remain unsupported. */
#include "sysv.h"
#include "../task.h"
#include "../mm/vm.h"
#include "../mm/mm.h"
#include "../mm/frame.h"
#include "../mm/heap.h"
#include "../mm/device_backing.h"
#include "../mm/anon_regions.h"
#include "../mm/prot.h"
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
#define SEM_GETPID 11U
#define SEM_GETVAL 12U
#define SEM_GETALL 13U
#define SEM_GETNCNT 14U
#define SEM_GETZCNT 15U
#define SEM_SETVAL 16U
#define SEM_SETALL 17U
#define SEM_VALUE_MAX 32767U
#define SHM_RDONLY 010000U
#define SHM_RND 020000U
#define SHM_PAGE_SIZE 4096ULL

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
    uint64_t count,ctime,atime,dtime;
    uint32_t refs,creator_uid,creator_gid,last_pid;
    bool deleted;
    uint64_t *shm_frames;
    uint64_t shm_page_count;
    struct nas_vm_device shm_device;
    /* Actual per-semaphore values, not fabricated GETVAL results. Semop's
     * waiting/undo semantics remain unsupported until implemented. */
    uint16_t sem_values[256];
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
    *out=(struct ipc_perm_lp64){.key=item->key,.mode=item->mode,
        .uid=item->creator_uid,.gid=item->creator_gid,
        .cuid=item->creator_uid,.cgid=item->creator_gid};
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
/* The physical-frame array belongs to a segment, not to one process.
 * Each user leaf takes its own PMM owner via the normal MM fault handler.
 * IPC_RMID hides the key immediately, but existing attachments keep
 * the segment alive; a late attach by id is supported while still pinned. */
static void shm_destroy(struct ipc_object *p) {
    if(!p || p->refs)__builtin_trap();
    for(uint64_t i=0;i<p->shm_page_count;i++)
        if(p->shm_frames && p->shm_frames[i] &&
           !nas_frame_put(p->shm_frames[i]))__builtin_trap();
    if(p->shm_frames && !nas_heap_free(p->shm_frames))__builtin_trap();
    *p=(struct ipc_object){0};
}
static bool shm_get(void *context) {
    struct ipc_object *p=context;
    if(!p || !p->used || p->refs==UINT32_MAX)return false;
    ++p->refs;
    return true;
}
static void shm_put(void *context) {
    struct ipc_object *p=context;
    if(!p || !p->used || !p->refs)__builtin_trap();
    if(!--p->refs && p->deleted)shm_destroy(p);
}
static int shm_page(void *context,uint64_t offset,uint64_t *frame) {
    struct ipc_object *p=context;
    if(!frame)return -EINVAL;
    *frame=0;
    if(!p || !p->used || !p->shm_frames ||
       (offset&(SHM_PAGE_SIZE-1)) || offset>=p->count)
        return -EINVAL;
    uint64_t index=offset/SHM_PAGE_SIZE;
    if(index>=p->shm_page_count)return -EINVAL;
    if(!p->shm_frames[index]) {
        uint64_t physical=nas_frame_alloc();
        if(!physical)return -ENOMEM;
        memset((void *)(uintptr_t)(nas_hhdm_offset()+physical),
               0,SHM_PAGE_SIZE);
        p->shm_frames[index]=physical;
    }
    *frame=p->shm_frames[index]; /* borrowed, MM obtains its own ref */
    return 0;
}
static bool shm_access_allowed(const struct ipc_object *p,bool writable) {
    const struct nas_credentials *c=&nas_current_task->credentials;
    if(c->euid==0)return true;
    unsigned shift=0;
    if(c->euid==p->creator_uid)shift=6;
    else {
        bool group=c->egid==p->creator_gid;
        for(uint32_t i=0;i<c->group_count && !group;i++)
            group=c->groups[i]==p->creator_gid;
        if(group)shift=3;
    }
    unsigned rights=(p->mode>>shift)&7U;
    return (rights&4U) && (!writable || (rights&2U));
}
static uint64_t shm_attach(uint64_t id,uint64_t requested,uint64_t flags) {
    struct ipc_object *p=by_id(shared,id);
    if(!p || (flags & ~(uint64_t)(SHM_RDONLY|SHM_RND)))
        return (uint64_t)-EINVAL;
    if(!shm_access_allowed(p,!(flags&SHM_RDONLY)))
        return (uint64_t)-EACCES;
    uint64_t bytes=(p->count+SHM_PAGE_SIZE-1)&~(SHM_PAGE_SIZE-1);
    uint64_t map_flags=1U; /* MAP_SHARED, never an anonymous private copy */
    if(requested) {
        if(requested&(SHM_PAGE_SIZE-1)) {
            if(!(flags&SHM_RND))return (uint64_t)-EINVAL;
            requested&=~(SHM_PAGE_SIZE-1);
        }
        if(!nas_anon_fixed_range_allowed(requested,bytes))
            return (uint64_t)-EINVAL;
        if(nas_anon_intersects(&nas_current_task->mm->mmap_vmas,
                               requested,bytes))
            return (uint64_t)-EINVAL; /* shmat cannot replace a live VMA */
        map_flags|=0x10U; /* MAP_FIXED, after independent overlap preflight */
    }
    if(!p->shm_device.get(p))return (uint64_t)-ENOMEM;
    /* Always consumes the preceding get, including on mapping failure. */
    uint64_t result=nas_vm_map_owned_device(requested,p->count,
           NAS_VM_READ|((flags&SHM_RDONLY)?0U:NAS_VM_WRITE),
           map_flags,&p->shm_device);
    if((int64_t)result>=0) {
        p->atime=now_seconds();
        p->last_pid=(uint32_t)nas_current_task->tgid;
    }
    return result;
}
static uint64_t shm_detach(uint64_t addr) {
    if(!addr || (addr&(SHM_PAGE_SIZE-1)))return (uint64_t)-EINVAL;
    struct nas_anon_regions *regions=&nas_current_task->mm->mmap_vmas;
    for(unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *v=&regions->slots[i];
        if(!v->end || v->start!=addr ||
           v->kind!=NAS_VMA_DEVICE_SHARED || !v->device ||
           v->device->page!=shm_page)continue;
        struct ipc_object *p=v->device->context;
        uint64_t bytes=v->end-v->start;
        /* Retain one owner until the unmap commits; IPC_RMID may already
         * have been issued and the last VMA put may otherwise destroy p. */
        if(!p->shm_device.get(p))return (uint64_t)-EINVAL;
        uint64_t result=nas_vm_munmap(addr,bytes);
        if((int64_t)result>=0) {
            p->dtime=now_seconds();
            p->last_pid=(uint32_t)nas_current_task->tgid;
        }
        p->shm_device.put(p);
        return result;
    }
    return (uint64_t)-EINVAL;
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
        if(!p->used || p->deleted || p->key!=signed_key)continue;
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
                               .creator_uid=nas_current_task->credentials.euid,
                               .creator_gid=nas_current_task->credentials.egid,
                               .count=kind==NAS_SYSVIPC_MSG?16384U:count,
                               .ctime=now_seconds()};
        if(kind==NAS_SYSVIPC_SHM) {
            uint64_t pages=(count+SHM_PAGE_SIZE-1)/SHM_PAGE_SIZE;
            if(pages>SIZE_MAX/sizeof(uint64_t)) {
                *p=(struct ipc_object){0};return (uint64_t)-ENOMEM;
            }
            p->shm_frames=nas_heap_calloc((size_t)pages,sizeof(uint64_t));
            if(!p->shm_frames) {
                *p=(struct ipc_object){0};return (uint64_t)-ENOMEM;
            }
            p->shm_page_count=pages;
            p->shm_device=(struct nas_vm_device){
                .context=p,.extent_bytes=count,
                .get=shm_get,.put=shm_put,.page=shm_page
            };
        }
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
    if(operation==IPC_RMID) {
        if(kind!=NAS_SYSVIPC_SHM){*p=(struct ipc_object){0};return 0;}
        p->deleted=true; /* no new lookup by key; existing id stays live */
        p->key=IPC_PRIVATE;
        if(!p->refs)shm_destroy(p);
        return 0;
    }
    if(operation==IPC_SET)return (uint64_t)-EOPNOTSUPP;
    if(operation!=IPC_STAT)return (uint64_t)-EINVAL;
    if(!buffer)return (uint64_t)-EFAULT;
    if(kind==NAS_SYSVIPC_SHM) {
        struct shmid_ds_lp64 ds={0};
        metadata(p,&ds.perm);
        ds.size=p->count;ds.ctime=p->ctime;
        ds.atime=p->atime;ds.dtime=p->dtime;
        ds.nattch=p->refs;
        ds.cpid=(int32_t)p->creator_pid;
        ds.lpid=(int32_t)p->last_pid;
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

/* semctl(semid, semnum, cmd, union semun): the fourth Linux syscall
 * argument is the union's scalar bits, unlike semctl IPC_STAT's pointer.
 * GETVAL/GETALL expose the zero-initialized and explicitly SET values;
 * they never claim that semop or SEM_UNDO has been implemented. */
static uint64_t semaphore_control(uint64_t id,uint64_t index,uint64_t cmd,
                                  uint64_t arg) {
    struct ipc_object *p=by_id(semaphores,id);
    if(!p)return (uint64_t)-EINVAL;
    if(cmd & ~(uint64_t)(IPC_64|0xffU))return (uint64_t)-EINVAL;
    unsigned operation=(unsigned)cmd & ~IPC_64;
    if(operation==IPC_RMID || operation==IPC_STAT || operation==IPC_SET)
        return control(NAS_SYSVIPC_SEM,id,cmd,arg);
    if(operation==SEM_GETALL || operation==SEM_SETALL) {
        if(!arg)return (uint64_t)-EFAULT;
        uint16_t values[256];
        uint64_t length=p->count*sizeof(values[0]);
        if(operation==SEM_GETALL) {
            for(unsigned i=0;i<p->count;i++)values[i]=p->sem_values[i];
            return nas_user_write_bytes(arg,(const uint8_t *)values,length) ?
                   0 : (uint64_t)-EFAULT;
        }
        if(!nas_user_read_bytes(arg,(uint8_t *)values,length))
            return (uint64_t)-EFAULT;
        for(unsigned i=0;i<p->count;i++)
            if(values[i]>SEM_VALUE_MAX)return (uint64_t)-ERANGE;
        for(unsigned i=0;i<p->count;i++)p->sem_values[i]=values[i];
        return 0;
    }
    if(index>=p->count)return (uint64_t)-EINVAL;
    switch(operation) {
    case SEM_GETVAL:return p->sem_values[index];
    case SEM_SETVAL:
        if(arg>SEM_VALUE_MAX)return (uint64_t)-ERANGE;
        p->sem_values[index]=(uint16_t)arg;
        return 0;
    case SEM_GETPID:case SEM_GETNCNT:case SEM_GETZCNT:
        /* No semop has run or blocked, so these lifecycle counters are 0. */
        return 0;
    default:return (uint64_t)-EINVAL;
    }
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
        if(kind==NAS_SYSVIPC_SEM)return semaphore_control(a,b,c,d);
        return control(kind,a,b,c);
    }
    if(kind==NAS_SYSVIPC_SHM) {
        if(operation==2)return shm_attach(a,b,c);
        if(operation==3)return shm_detach(a);
    }
    return (uint64_t)-ENOSYS;
}
