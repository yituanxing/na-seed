#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../errno.h"
#include "../fd.h"
#include "../task.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "namespace_syscall.h"
#include "path_request.h"
#include "vfs.h"
#include "inotify.h"

/* inotify watches identify live VFS inodes but deliberately DO NOT pin
 * their inode leases: a watch must not turn unlink(2) into EBUSY on a
 * filesystem whose open-inode unlink contract is limited. Successful VFS
 * unlink and mount teardown invalidate identities before reuse. */
#define IN_WATCH_MAX 32U
#define IN_EVENT_MAX 64U
#define IN_NAME_MAX 256U
#define IN_CLOEXEC  0x80000U
#define IN_NONBLOCK 0x800U
#define IN_ONLYDIR 0x01000000U
#define IN_DONT_FOLLOW 0x02000000U
#define IN_EXCL_UNLINK 0x04000000U
#define IN_MASK_CREATE 0x10000000U
#define IN_MASK_ADD 0x20000000U
#define IN_ONESHOT 0x80000000U
#define IN_ALL_EVENTS 0x00000FFFU
#define IN_WATCH_OPTIONS (IN_ONLYDIR|IN_DONT_FOLLOW|IN_EXCL_UNLINK|IN_MASK_CREATE|IN_MASK_ADD|IN_ONESHOT)
#define IN_WIRE_HEADER 16U
struct in_watch {
    bool used;
    int32_t wd;
    uint32_t mask;
    struct nas_fs_node identity; /* borrowed VALUE, never dereferenced */
};
struct in_event {
    int32_t wd;
    uint32_t mask,cookie,length;
    char name[IN_NAME_MAX];
};
struct in_instance {
    uint32_t next_wd;
    unsigned head,count;
    bool overflow_pending;
    struct in_watch watch[IN_WATCH_MAX];
    struct in_event queue[IN_EVENT_MAX];
};
static struct in_instance *instances[NAS_OPEN_FILE_LIMIT];
static uint32_t next_cookie;
static bool same_inode(const struct nas_fs_node *a,
                       const struct nas_fs_node *b) {
    return a->mount==b->mount && a->mount_id==b->mount_id &&
           a->filesystem==b->filesystem && a->inode==b->inode &&
           a->ino==b->ino;
}
static int fd_instance(uint64_t fd,unsigned *slot) {
    if(fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_INOTIFY)return -EBADF;
    unsigned s=nas_current_task->fds->entries[fd].slot;
    if(s>=NAS_OPEN_FILE_LIMIT || !nas_open_files[s].refcount ||
       !instances[s])return -EBADF;
    *slot=s;return 0;
}
static struct in_event *last_event(struct in_instance *in) {
    if(!in->count)return NULL;
    return &in->queue[(in->head+in->count-1U)%IN_EVENT_MAX];
}
static void queue_event(struct in_instance *in,int32_t wd,uint32_t mask,
                        uint32_t cookie,const char *name) {
    unsigned len=0,actual=0;
    if(name && name[0]) {
        actual=(unsigned)strnlen(name,IN_NAME_MAX-1U)+1U;
        len=actual;
        len=(len+3U)&~3U;
    }
    struct in_event *last=last_event(in);
    /* Linux merges identical unread adjacent events; a full queue instead
     * schedules precisely one overflow record after room becomes available. */
    if(last && last->wd==wd && last->mask==mask &&
       last->cookie==cookie && last->length==len &&
       (!len || !strncmp(last->name,name,len)))return;
    if(in->count==IN_EVENT_MAX) {in->overflow_pending=true;return;}
    struct in_event *e=&in->queue[(in->head+in->count++)%IN_EVENT_MAX];
    *e=(struct in_event){.wd=wd,.mask=mask,.cookie=cookie,.length=len};
    if(name && actual)memcpy(e->name,name,actual);
}
static uint32_t event_bytes(const struct in_event *e) {
    return IN_WIRE_HEADER+e->length;
}
static bool queue_peek(const struct in_instance *in,unsigned at,
                       struct in_event *out) {
    if(at<in->count) {
        *out=in->queue[(in->head+at)%IN_EVENT_MAX];return true;
    }
    if(at==in->count && in->overflow_pending) {
        *out=(struct in_event){.wd=-1,.mask=NAS_IN_Q_OVERFLOW};return true;
    }
    return false;
}
static void queue_pop(struct in_instance *in) {
    if(in->count) {in->head=(in->head+1U)%IN_EVENT_MAX;--in->count;}
    else in->overflow_pending=false;
}
uint64_t nas_inotify_init1(uint64_t flags) {
    if(flags&~(IN_NONBLOCK|IN_CLOEXEC))return (uint64_t)-EINVAL;
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(nas_current_task,0,1,fds,slots,"inotify_init1");
    if(rc)return (uint64_t)(int64_t)rc;
    struct in_instance *instance=nas_heap_calloc(1,sizeof(*instance));
    if(!instance)return (uint64_t)-ENOMEM;
    instance->next_wd=1;
    unsigned slot=slots[0];
    if(!nas_fd_install_new(nas_current_task,fds[0],slot,FD_INOTIFY,
                           0U|(flags&IN_NONBLOCK),(flags&IN_CLOEXEC)!=0)) {
        (void)nas_heap_free(instance);return (uint64_t)-EBADF;
    }
    instances[slot]=instance;
    return fds[0];
}
uint64_t nas_inotify_add_watch(uint64_t fd,uint64_t user_path,uint64_t mask) {
    unsigned slot=0;
    int rc=fd_instance(fd,&slot);
    if(rc)return (uint64_t)(int64_t)rc;
    if(mask&~(IN_ALL_EVENTS|IN_WATCH_OPTIONS))return (uint64_t)-EINVAL;
    struct nas_fs_path_request request={0};
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    rc=nas_fs_user_lookup_begin((uint64_t)-100,user_path,
                 !(mask&IN_DONT_FOLLOW),&request,&path,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    if((mask&IN_ONLYDIR) && !node.is_dir)rc=-ENOTDIR;
    if(path.dentry) {
        int dropped=nas_vfs_path_put(&path);
        if(dropped && !rc)rc=dropped;
    }
    nas_fs_path_end(&request);
    if(rc)return (uint64_t)(int64_t)rc;
    struct in_instance *in=instances[slot];
    struct in_watch *free_watch=NULL;
    for(unsigned i=0;i<IN_WATCH_MAX;i++) {
        struct in_watch *watch=&in->watch[i];
        if(!watch->used) {if(!free_watch)free_watch=watch;continue;}
        if(!same_inode(&watch->identity,&node))continue;
        if(mask&IN_MASK_CREATE)return (uint64_t)-EEXIST;
        const uint32_t events=(uint32_t)mask&IN_ALL_EVENTS;
        watch->mask=(mask&IN_MASK_ADD)?
                  (watch->mask|events):(events|(mask&IN_ONESHOT));
        return (uint32_t)watch->wd;
    }
    if(!free_watch)return (uint64_t)-ENOSPC;
    if(in->next_wd==INT32_MAX)return (uint64_t)-ENOSPC;
    *free_watch=(struct in_watch){.used=true,.wd=(int32_t)in->next_wd++,
       .mask=(uint32_t)mask&(IN_ALL_EVENTS|IN_ONESHOT),.identity=node};
    return (uint32_t)free_watch->wd;
}
uint64_t nas_inotify_rm_watch(uint64_t fd,uint64_t wd) {
    unsigned slot=0;
    int rc=fd_instance(fd,&slot);
    if(rc)return (uint64_t)(int64_t)rc;
    struct in_instance *in=instances[slot];
    for(unsigned i=0;i<IN_WATCH_MAX;i++) {
        struct in_watch *w=&in->watch[i];
        if(w->used && w->wd==(int32_t)wd) {
            queue_event(in,w->wd,NAS_IN_IGNORED,0,NULL);
            *w=(struct in_watch){0};return 0;
        }
    }
    return (uint64_t)-EINVAL;
}
uint64_t nas_inotify_read_plan(unsigned slot,uint64_t requested) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !instances[slot] ||
       !nas_open_files[slot].refcount)return (uint64_t)-EBADF;
    if(!requested)return 0;
    struct in_instance *in=instances[slot];
    if(!in->count && !in->overflow_pending)
        return (nas_open_files[slot].status_flags&IN_NONBLOCK)?
               (uint64_t)-EAGAIN:(uint64_t)-EOPNOTSUPP;
    uint64_t total=0;
    struct in_event e;
    for(unsigned i=0;queue_peek(in,i,&e);i++) {
        uint64_t next=event_bytes(&e);
        if(total+next>requested || total+next>1024U)break;
        total+=next;
    }
    return total ? total:(uint64_t)-EINVAL;
}
uint64_t nas_inotify_read(unsigned slot,uint8_t *dst,uint64_t count) {
    uint64_t planned=nas_inotify_read_plan(slot,count);
    if((int64_t)planned<0 || !planned)return planned;
    if(!dst)return (uint64_t)-EFAULT;
    struct in_instance *in=instances[slot];
    unsigned copied=0;
    while(copied<planned) {
        struct in_event e;
        if(!queue_peek(in,0,&e))break;
        memcpy(dst+copied,&e,IN_WIRE_HEADER);
        if(e.length)memcpy(dst+copied+IN_WIRE_HEADER,e.name,e.length);
        copied+=event_bytes(&e);
        queue_pop(in);
    }
    return copied;
}
uint16_t nas_inotify_poll_slot(unsigned slot,uint16_t events) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !instances[slot] ||
       !nas_open_files[slot].refcount)return 0;
    return (events&1U) && (instances[slot]->count ||
                            instances[slot]->overflow_pending)?1U:0;
}
uint16_t nas_inotify_poll_fd(uint64_t fd,uint16_t events) {
    unsigned slot=0;
    return fd_instance(fd,&slot)?0:nas_inotify_poll_slot(slot,events);
}
void nas_inotify_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !instances[slot])return;
    (void)nas_heap_free(instances[slot]);
    instances[slot]=NULL;
}
uint32_t nas_inotify_rename_cookie(void) {
    if(!++next_cookie)++next_cookie;
    return next_cookie;
}
static void publish(const struct nas_fs_node *node,const char *name,
                    uint32_t mask,uint32_t cookie,bool isdir,bool child) {
    if(!node)return;
    for(unsigned s=0;s<NAS_OPEN_FILE_LIMIT;s++) {
        struct in_instance *in=instances[s];
        if(!in)continue;
        for(unsigned i=0;i<IN_WATCH_MAX;i++) {
            struct in_watch *w=&in->watch[i];
            if(!w->used || !same_inode(&w->identity,node) ||
               !(w->mask&mask))continue;
            queue_event(in,w->wd,mask|(isdir?NAS_IN_ISDIR:0U),cookie,
                        child?name:NULL);
            if(w->mask&IN_ONESHOT) {
                queue_event(in,w->wd,NAS_IN_IGNORED,0,NULL);
                *w=(struct in_watch){0};
            }
        }
    }
}
void nas_inotify_emit_child(const struct nas_fs_node *parent,const char *name,
                           uint32_t mask,uint32_t cookie,bool is_directory) {
    publish(parent,name,mask,cookie,is_directory,true);
}
void nas_inotify_emit_node(const struct nas_fs_node *node,uint32_t mask) {
    publish(node,NULL,mask,0,node?node->is_dir:false,false);
}
void nas_inotify_erase_node(const struct nas_fs_node *node) {
    if(!node)return;
    for(unsigned s=0;s<NAS_OPEN_FILE_LIMIT;s++) {
        struct in_instance *in=instances[s];
        if(!in)continue;
        for(unsigned i=0;i<IN_WATCH_MAX;i++) {
            struct in_watch *w=&in->watch[i];
            if(!w->used || !same_inode(&w->identity,node))continue;
            queue_event(in,w->wd,NAS_IN_DELETE_SELF,0,NULL);
            queue_event(in,w->wd,NAS_IN_IGNORED,0,NULL);
            *w=(struct in_watch){0};
        }
    }
}
void nas_inotify_erase_mount(const void *mount,uint64_t mount_id) {
    for(unsigned s=0;s<NAS_OPEN_FILE_LIMIT;s++) {
        struct in_instance *in=instances[s];
        if(!in)continue;
        for(unsigned i=0;i<IN_WATCH_MAX;i++) {
            struct in_watch *w=&in->watch[i];
            if(!w->used || w->identity.mount!=(const struct nas_vfs_mount *)mount ||
               w->identity.mount_id!=mount_id)continue;
            queue_event(in,w->wd,0x00002000U,0,NULL); /* IN_UNMOUNT */
            queue_event(in,w->wd,NAS_IN_IGNORED,0,NULL);
            *w=(struct in_watch){0};
        }
    }
}
