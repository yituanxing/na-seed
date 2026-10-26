#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../pipe.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "../time/clock.h"
#include "../lib/string.h"
#include "file_ops.h"
#include "../task/wait.h"
#include "epoll.h"
#include "unix_socket.h"
#include "eventfd.h"

/* Open-description-owned bounded epoll instances, not process-specific
 * syscall aliases. fd slot identity is shared across fork() and dup().
 * User pointers are touched only with the caller's current MMU active.
 * No kernel stack continuation, no app-name or ISA-dependent shortcuts. */
#define NAS_EPOLL_EVENTS_MAX NAS_FD_LIMIT
#define EP_IN   0x0001U
#define EP_PRI  0x0002U
#define EP_OUT  0x0004U
#define EP_ERR  0x0008U
#define EP_HUP  0x0010U
#define EP_RDHUP 0x2000U
#define EP_ONESHOT 0x40000000U
#define EP_EDGE 0x80000000U
#define EP_ALLOWED (EP_IN|EP_PRI|EP_OUT|EP_ERR|EP_HUP|EP_RDHUP|EP_ONESHOT|EP_EDGE)
#define EP_CLOEXEC 0x80000U

struct ep_watch {
    bool used,disabled;
    uint8_t slot;
    uint8_t kind;
    uint16_t fd;
    uint16_t last_ready;
    uint32_t events;
    uint64_t data;
};
struct ep_state {struct ep_watch watches[NAS_EPOLL_EVENTS_MAX];};
static struct ep_state epolls[NAS_OPEN_FILE_LIMIT];

#if defined(__x86_64__)
#define EP_EVENT_BYTES 12U
#define EP_DATA_OFFSET 4U
#else
/* asm-generic 64-bit epoll_event aligns data on an 8-byte boundary. */
#define EP_EVENT_BYTES 16U
#define EP_DATA_OFFSET 8U
#endif

static bool valid_fd_task(const struct nas_task *task,uint64_t fd){
    return task && fd<NAS_FD_LIMIT &&
        task->fds->entries[fd].kind!=FD_CLOSED &&
        task->fds->entries[fd].slot<NAS_OPEN_FILE_LIMIT &&
        nas_open_files[task->fds->entries[fd].slot].refcount;
}
static bool instance_slot(const struct nas_task *task,uint64_t fd,
                          unsigned *slot){
    if(!valid_fd_task(task,fd) || task->fds->entries[fd].kind!=FD_EPOLL)
        return false;
    *slot=task->fds->entries[fd].slot;
    return true;
}
static uint16_t ready_now(const struct ep_watch *w){
    if(!w->used || w->disabled ||
       w->slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[w->slot].refcount)
        return 0;
    /* All kinds share one open-description operation. Future AF_INET,
     * block devices and filesystems only need to implement their own
     * poll_description callback; epoll never learns the backend type. */
    return nas_fs_poll_description(w->slot,
        (uint16_t)(w->events & (EP_IN|EP_PRI|EP_OUT|EP_RDHUP)));
}
static bool instance_ready(unsigned slot){
    for(unsigned i=0;i<NAS_EPOLL_EVENTS_MAX;i++){
        struct ep_watch *w=&epolls[slot].watches[i];
        uint16_t ready=ready_now(w);
        if(!ready)w->last_ready=0;
        else if(!(w->events&EP_EDGE) || (ready & ~w->last_ready))return true;
    }
    return false;
}
uint64_t nas_epoll_create1(uint64_t flags){
    if(flags&~EP_CLOEXEC)return (uint64_t)-EINVAL;
    struct nas_task *task=nas_current_task;
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(task,0,1,fds,slots,"epoll_create1");
    if(rc)return (uint64_t)(int64_t)rc;
    unsigned fd=fds[0],slot=slots[0];
    epolls[slot]=(struct ep_state){0};
    if(!nas_fd_install_new(task,fd,slot,FD_EPOLL,0,
                           (flags&EP_CLOEXEC)!=0))
        return (uint64_t)-EBADF;
    return fd;
}
static bool decode_event(uint64_t addr,uint32_t *events,uint64_t *data){
    uint8_t bytes[EP_EVENT_BYTES];
    if(!addr || !nas_user_read_bytes(addr,bytes,sizeof(bytes)))return false;
    memcpy(events,bytes,4);
    memcpy(data,bytes+EP_DATA_OFFSET,8);
    return true;
}
uint64_t nas_epoll_ctl(uint64_t epfd,uint64_t op,uint64_t fd,uint64_t event){
    struct nas_task *task=nas_current_task;
    unsigned slot;
    if(!instance_slot(task,epfd,&slot))return (uint64_t)-EBADF;
    if(op<1 || op>3)return (uint64_t)-EINVAL;
    if(!valid_fd_task(task,fd))return (uint64_t)-EBADF;
    if(task->fds->entries[fd].kind==FD_EPOLL)return (uint64_t)-ELOOP;
    if(epfd==fd)return (uint64_t)-EINVAL;
    const unsigned target_slot=task->fds->entries[fd].slot;
    struct ep_watch *slot_found=0,*free_watch=0;
    for(unsigned i=0;i<NAS_EPOLL_EVENTS_MAX;i++){
        struct ep_watch *w=&epolls[slot].watches[i];
        if(!w->used) {if(!free_watch)free_watch=w;continue;}
        if(w->fd==fd && w->slot==target_slot)slot_found=w;
    }
    if(op==2){ /* EPOLL_CTL_DEL */
        if(!slot_found)return (uint64_t)-ENOENT;
        *slot_found=(struct ep_watch){0};
        return 0;
    }
    uint32_t events;
    uint64_t data;
    if(!decode_event(event,&events,&data))return (uint64_t)-EFAULT;
    if(events & ~EP_ALLOWED)return (uint64_t)-EINVAL;
    if(op==1){ /* EPOLL_CTL_ADD */
        if(slot_found)return (uint64_t)-EEXIST;
        if(!free_watch)return (uint64_t)-ENOSPC;
        *free_watch=(struct ep_watch){0};
        free_watch->used=true;
        free_watch->fd=(uint16_t)fd;
        free_watch->slot=(uint8_t)target_slot;
        free_watch->kind=task->fds->entries[fd].kind;
        slot_found=free_watch;
    }else if(!slot_found)return (uint64_t)-ENOENT;
    slot_found->events=events;
    slot_found->data=data;
    slot_found->disabled=false;
    slot_found->last_ready=0; /* MOD rearms EPOLLONESHOT. */
    return 0;
}
uint64_t nas_epoll_wait(uint64_t epfd,uint64_t out,uint64_t maximum,uint64_t timeout){
    struct nas_task *task=nas_current_task;
    unsigned slot;
    if(!instance_slot(task,epfd,&slot))return (uint64_t)-EBADF;
    if(maximum==0 || (int64_t)maximum<0 || maximum>INT32_MAX)
        return (uint64_t)-EINVAL;
    const uint64_t maxcopy=maximum<NAS_EPOLL_EVENTS_MAX?maximum:NAS_EPOLL_EVENTS_MAX;
    if(out>=USER_LIMIT || maxcopy>(USER_LIMIT-out)/EP_EVENT_BYTES)
        return (uint64_t)-EFAULT;
    uint64_t done=0;
    for(unsigned i=0;i<NAS_EPOLL_EVENTS_MAX && done<maximum;i++){
        struct ep_watch *w=&epolls[slot].watches[i];
        uint16_t ready=ready_now(w);
        if(!ready) {w->last_ready=0;continue;}
        if((w->events&EP_EDGE) && !(ready & ~w->last_ready))continue;
        uint8_t bytes[EP_EVENT_BYTES]={0};
        uint32_t events=ready;
        memcpy(bytes,&events,sizeof(events));
        memcpy(bytes+EP_DATA_OFFSET,&w->data,sizeof(w->data));
        if(!nas_user_write_bytes(out+done*EP_EVENT_BYTES,bytes,sizeof(bytes)))
            return (uint64_t)-EFAULT;
        done++;
        w->last_ready=ready;
        if(w->events&EP_ONESHOT)w->disabled=true;
    }
    if(done)return done;
    if((int32_t)(uint32_t)timeout==0 ||
       (task->wait.deadline_active &&
        nas_time_deadline_expired(nas_arch_monotonic_ns(),task->wait.deadline_ns)))
        return 0;
    return (uint64_t)-EAGAIN;
}
uint16_t nas_epoll_description_poll(unsigned slot,uint16_t interests) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return 0;
    return instance_ready(slot)?(interests&EP_IN):0;
}
uint16_t nas_epoll_fd_poll(uint64_t fd,uint16_t interests){
    unsigned slot;
    if(!instance_slot(nas_current_task,fd,&slot))return 0;
    return nas_epoll_description_poll(slot,interests);
}
void nas_epoll_arm_current_task(uint64_t epfd){
    unsigned slot;
    struct nas_task *task=nas_current_task;
    nas_wait_io_clear(task);
    if(!instance_slot(task,epfd,&slot))return;
    for(unsigned i=0;i<NAS_EPOLL_EVENTS_MAX;i++){
        struct ep_watch *w=&epolls[slot].watches[i];
        if(!w->used || w->disabled)continue;
        /* Register ANY watched open description (including eventfd and
         * future network/device providers). No type switch in epoll.
         * TTY's external-RX pump is a separate scheduler responsibility. */
        uint16_t interest=0;
        if(w->events&(EP_IN|EP_PRI|EP_RDHUP))interest|=EP_IN;
        if(w->events&EP_OUT)interest|=EP_OUT;
        nas_wait_io_arm(task,w->slot,interest,
                        (w->kind==FD_TTY || w->kind==FD_UART));
    }
}
bool nas_epoll_task_ready(const struct nas_task *task){
    if(!task || task->wait.blocked_args[0]>=NAS_FD_LIMIT ||
       task->fds->entries[task->wait.blocked_args[0]].kind!=FD_EPOLL)return true;
    unsigned slot=task->fds->entries[task->wait.blocked_args[0]].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return true;
    return instance_ready(slot);
}
void nas_epoll_target_final_close(unsigned target){
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++){
        if(!nas_open_files[i].refcount)continue;
        for(unsigned j=0;j<NAS_EPOLL_EVENTS_MAX;j++){
            struct ep_watch *w=&epolls[i].watches[j];
            if(w->used && w->slot==target)*w=(struct ep_watch){0};
        }
    }
}
void nas_epoll_release(unsigned slot){
    if(slot<NAS_OPEN_FILE_LIMIT)epolls[slot]=(struct ep_state){0};
}
