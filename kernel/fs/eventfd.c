#include <stdint.h>
#include <stdbool.h>
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "file_ops.h"
#include "eventfd.h"

/* Real Linux eventfd counter is owned by one shared open-description slot,
 * not an fd number or process. Current non-preemptible kernel serializes
 * read/write mutations without an unnecessary SMP lock. */
#define EFD_SEMAPHORE 1U
#define EFD_NONBLOCK  0x800U
#define EFD_CLOEXEC  0x80000U
#define EFD_MAX (UINT64_MAX-1ULL)
#define EFD_IN 1U
#define EFD_OUT 4U
static uint64_t counters[NAS_OPEN_FILE_LIMIT];
static bool semaphore_mode[NAS_OPEN_FILE_LIMIT];
static bool eventfd_slot(uint64_t fd,unsigned *slot){
    if(fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->descriptors[fd]!=FD_EVENTFD)return false;
    unsigned s=nas_current_task->fds->file_slot[fd];
    if(s>=NAS_OPEN_FILE_LIMIT || !nas_open_files[s].refcount)return false;
    *slot=s;
    return true;
}
uint64_t nas_eventfd_create(uint64_t init,uint64_t flags){
    if(flags&~(EFD_SEMAPHORE|EFD_NONBLOCK|EFD_CLOEXEC))
        return (uint64_t)-EINVAL;
    struct nas_task *t=nas_current_task;
    for(unsigned fd=0;fd<NAS_FD_LIMIT;fd++){
        if(t->fds->descriptors[fd]!=FD_CLOSED)continue;
        for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++){
            if(nas_open_files[slot].refcount)continue;
            counters[slot]=(uint32_t)init;
            semaphore_mode[slot]=(flags&EFD_SEMAPHORE)!=0;
            nas_open_files[slot]=(struct nas_open_file){0};
            nas_open_files[slot].refcount=1;
            nas_open_files[slot].status_flags=2ULL|(flags&EFD_NONBLOCK);
            nas_open_files[slot].ops=nas_fs_ops_for_kind(FD_EVENTFD);
            t->fds->descriptors[fd]=FD_EVENTFD;
            t->fds->file_slot[fd]=(uint8_t)slot;
            t->fds->fd_cloexec[fd]=(flags&EFD_CLOEXEC)!=0;
            return fd;
        }
        nas_fd_report_exhaustion("eventfd2");
        return (uint64_t)-ENFILE;
    }
    return (uint64_t)-EMFILE;
}
uint64_t nas_eventfd_read(uint64_t fd,uint64_t addr,uint64_t count){
    unsigned slot;
    if(!eventfd_slot(fd,&slot))return (uint64_t)-EBADF;
    if(count<8)return (uint64_t)-EINVAL;
    if(!counters[slot])
        return nas_open_files[slot].status_flags&EFD_NONBLOCK?
            (uint64_t)-EAGAIN:(uint64_t)-EOPNOTSUPP;
    uint64_t value=semaphore_mode[slot]?1:counters[slot];
    if(!nas_user_write_u64(addr,value))return (uint64_t)-EFAULT;
    counters[slot]-=value;
    return 8;
}
uint64_t nas_eventfd_write(uint64_t fd,uint64_t addr,uint64_t count){
    unsigned slot;
    if(!eventfd_slot(fd,&slot))return (uint64_t)-EBADF;
    if(count<8)return (uint64_t)-EINVAL;
    uint64_t value=0;
    if(!nas_user_read_u64(addr,&value))return (uint64_t)-EFAULT;
    if(value==UINT64_MAX)return (uint64_t)-EINVAL;
    if(value>EFD_MAX-counters[slot])
        return nas_open_files[slot].status_flags&EFD_NONBLOCK?
            (uint64_t)-EAGAIN:(uint64_t)-EOPNOTSUPP;
    counters[slot]+=value;
    return 8;
}
bool nas_eventfd_ready(unsigned slot,bool writing){
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return true;
    return writing?counters[slot]<EFD_MAX:counters[slot]!=0;
}
uint16_t nas_eventfd_poll_slot(unsigned slot,uint16_t events){
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return 0;
    uint16_t v=0;
    if((events&EFD_IN) && counters[slot])v|=EFD_IN;
    if((events&EFD_OUT) && counters[slot]<EFD_MAX)v|=EFD_OUT;
    return v;
}
uint16_t nas_eventfd_poll(uint64_t fd,uint16_t events){
    unsigned slot;
    return eventfd_slot(fd,&slot)?nas_eventfd_poll_slot(slot,events):0;
}
void nas_eventfd_release(unsigned slot){
    if(slot<NAS_OPEN_FILE_LIMIT){counters[slot]=0;semaphore_mode[slot]=false;}
}
