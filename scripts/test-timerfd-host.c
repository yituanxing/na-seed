#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/fs/timerfd.h"

/* Real timerfd provider, deterministic hardware clock and real File-slot
 * ownership. A timer may become ready while the caller is not scheduled. */
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static struct nas_task process;
static struct nas_fd_table descriptors;
struct nas_task *nas_current_task=&process;
static uint64_t monotonic_ns;
uint64_t nas_arch_monotonic_ns(void){return monotonic_ns;}
bool nas_time_realtime_ns(uint64_t *out){*out=100000000000ULL+monotonic_ns;return true;}
bool nas_user_read_bytes(uint64_t addr,uint8_t *dst,uint64_t size){
    if(!addr)return false;
    memcpy(dst,(void *)(uintptr_t)addr,(size_t)size);
    return true;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *src,uint64_t size){
    if(!addr)return false;
    memcpy((void *)(uintptr_t)addr,src,(size_t)size);
    return true;
}
int nas_fd_reserve(const struct nas_task *t,unsigned minimum,unsigned count,
                   unsigned fds[2],unsigned slots[2],const char *operation){
    (void)operation;
    assert(t==&process && count==1);
    for(unsigned fd=minimum;fd<NAS_FD_LIMIT;fd++){
        if(t->fds->entries[fd].kind!=FD_CLOSED)continue;
        for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++){
            if(nas_open_files[slot].refcount)continue;
            fds[0]=fd;slots[0]=slot;
            return 0;
        }
    }
    return -EMFILE;
}
bool nas_fd_install_new(struct nas_task *t,unsigned fd,unsigned slot,
                        uint8_t kind,uint64_t flags,bool cloexec){
    assert(t==&process && !nas_open_files[slot].refcount);
    t->fds->entries[fd]=(struct nas_fd_entry){kind,(uint8_t)slot,cloexec};
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].status_flags=flags;
    return true;
}
static uint64_t ptr(const void *p){return (uint64_t)(uintptr_t)p;}
static void close_fd(unsigned fd){
    unsigned slot=descriptors.entries[fd].slot;
    descriptors.entries[fd]=(struct nas_fd_entry){0};
    if(!--nas_open_files[slot].refcount)nas_timerfd_release(slot);
}
int main(void){
    process.fds=&descriptors;
    uint64_t fd=nas_timerfd_create(1,0);
    assert(fd==0 && descriptors.entries[0].kind==FD_TIMERFD);
    assert((int64_t)nas_timerfd_create(22,0)==-EINVAL);
    assert((int64_t)nas_timerfd_create(1,1)==-EINVAL);
    unsigned slot=descriptors.entries[fd].slot;
    assert((int64_t)nas_timerfd_read_plan(slot,7)==-EINVAL);
    assert((int64_t)nas_timerfd_read_plan(slot,8)==-EOPNOTSUPP);
    assert(nas_timerfd_poll_fd(fd,1)==0);
    uint64_t one_shot[4]={0,0,3,0}, old[4]={0};
    assert(nas_timerfd_settime(fd,0,ptr(one_shot),ptr(old))==0);
    assert(old[0]==0 && old[2]==0);
    assert(nas_timerfd_gettime(fd,ptr(old))==0 && old[2]==3);
    monotonic_ns=2999999999ULL;
    assert(nas_timerfd_poll_fd(fd,1)==0);
    monotonic_ns=3000000000ULL;
    assert((nas_timerfd_poll_fd(fd,1)&0x41)==0x41);
    uint8_t bytes[16]={0};
    assert(nas_timerfd_read(slot,bytes,sizeof(bytes))==8 && bytes[0]==1);
    assert(nas_timerfd_poll_fd(fd,1)==0);
    /* Periodic expirations missed while the caller is not scheduled must
     * accumulate and be delivered exactly once to EVERY dup/fork alias. */
    uint64_t periodic[4]={0,100000000,0,200000000};
    assert(nas_timerfd_settime(fd,0,ptr(periodic),0)==0);
    descriptors.entries[6]=descriptors.entries[fd];
    nas_open_files[slot].refcount++;
    monotonic_ns=3700000000ULL;
    assert(nas_timerfd_poll_fd(6,1)&1);
    memset(bytes,0,sizeof(bytes));
    assert(nas_timerfd_read(slot,bytes,8)==8);
    assert(bytes[0]==6); /* at 3.2s then 3.3, 3.4, 3.5, 3.6, 3.7 */
    close_fd((unsigned)fd);
    monotonic_ns=3800000000ULL;
    assert(nas_timerfd_poll_fd(6,1)&1);
    assert(nas_timerfd_read(slot,bytes,8)==8 && bytes[0]==1);
    close_fd(6);
    uint64_t reopened=nas_timerfd_create(1,0x80800);
    assert(reopened==0 && descriptors.entries[reopened].cloexec);
    assert((int64_t)nas_timerfd_read_plan(slot,8)==-EAGAIN);
    /* Reused OpenFile slot must not inherit the previous timer's deadline. */
    assert(nas_timerfd_poll_fd(reopened,1)==0);
    uint64_t deadline[4]={0,0,4,0};
    assert(nas_timerfd_settime(reopened,1,ptr(deadline),0)==0);
    monotonic_ns=4000000000ULL;
    assert(nas_timerfd_poll_fd(reopened,1)&1);
    assert(nas_timerfd_read(slot,bytes,8)==8 && bytes[0]==1);
    /* Disarming and invalid timespec do not leak previous expirations. */
    uint64_t bad[4]={0,1000000000,1,0};
    assert((int64_t)nas_timerfd_settime(reopened,0,ptr(bad),0)==-EINVAL);
    uint64_t disarm[4]={0};
    assert(nas_timerfd_settime(reopened,0,ptr(disarm),0)==0);
    assert(nas_timerfd_poll_fd(reopened,1)==0);
    close_fd((unsigned)reopened);
    return 0;
}
