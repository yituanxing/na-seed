/* The actual epoll wait/ready code under a controlled File table. The only
 * fakes are the process FD edge, usercopy and backend readiness callback. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/errno.h"
#include "../kernel/task/wait.h"
#include "../kernel/fs/file_ops.h"

static struct nas_task task;
static struct nas_files files;
struct nas_task *nas_current_task=&task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static uint16_t backend_ready[NAS_OPEN_FILE_LIMIT];
static bool output_writable=true;

uint16_t nas_fs_poll_description_task(unsigned slot,uint16_t events,
                                     const struct nas_task *owner) {
    assert(owner==&task && slot<NAS_OPEN_FILE_LIMIT);
    return backend_ready[slot]&(events|0x18U);
}
bool nas_fd_hold_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       nas_open_files[slot].lease_token ||
       nas_open_files[slot].refcount==UINT16_MAX)return false;
    nas_open_files[slot].refcount++;
    return true;
}
bool nas_user_write_bytes(uint64_t addr,const uint8_t *bytes,size_t length) {
    if(!addr || !bytes)return false;
    memcpy((void *)(uintptr_t)addr,bytes,length);
    return true;
}
bool nas_user_prepare_write(uint64_t addr,uint64_t length) {
    (void)length;
    return addr && output_writable;
}
uint64_t nas_arch_monotonic_ns(void){return 1000000000ULL;}

/* Include the REAL epoll TU: this gives the regression a direct view of
 * its internal OpenFile-indexed watch registry for deterministic setup. */
#include "../kernel/fs/epoll.c"

void nas_fs_drop_kernel_description(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount);
    if(!--nas_open_files[slot].refcount) {
        nas_epoll_target_final_close(slot);
        nas_epoll_release(slot);
    }
}

int main(void) {
    files.refs=1;
    task.files=&files;
    task.fds=&files.table;
    task.fds->entries[3]=(struct nas_fd_entry){FD_EPOLL,40,0};
    nas_open_files[40].refcount=1;
    nas_open_files[50].refcount=1;
    epolls[40].watches[0]=(struct ep_watch){
        .used=true,.slot=50,.fd=7,.kind=FD_EVENTFD,
        .events=EP_IN,.data=UINT64_C(0x12345678)
    };
    uint8_t output[EP_EVENT_BYTES]={0};
    assert(nas_epoll_wait(3,(uint64_t)(uintptr_t)output,1,0)==0);

    /* The wait owns the OLD epoll instance despite close/rebind of epfd.
     * The new instance can be ready without waking the old waiter. */
    unsigned slot=NAS_OPEN_FILE_LIMIT;
    assert(nas_epoll_hold_instance(3,&slot)==0 && slot==40);
    assert(nas_open_files[40].refcount==2);
    task.wait.description_slot=slot;
    task.wait.description_release=nas_fs_drop_kernel_description;
    task.wait.blocked_args[0]=3;
    task.fds->entries[3]=(struct nas_fd_entry){0};
    nas_fs_drop_kernel_description(40);
    assert(nas_open_files[40].refcount==1);
    task.fds->entries[3]=(struct nas_fd_entry){FD_EPOLL,41,0};
    nas_open_files[41].refcount=1;
    nas_open_files[51].refcount=1;
    epolls[41].watches[0]=(struct ep_watch){
        .used=true,.slot=51,.fd=8,.kind=FD_EVENTFD,
        .events=EP_IN,.data=UINT64_C(0xeeee)
    };
    backend_ready[51]=EP_IN;
    assert(!nas_epoll_task_ready(&task));
    assert(nas_epoll_wait(3,(uint64_t)(uintptr_t)output,1,0)==1);
    uint64_t data=0;
    memcpy(&data,output+EP_DATA_OFFSET,8);
    assert(data==UINT64_C(0xeeee));

    backend_ready[50]=EP_IN;
    assert(nas_epoll_task_ready(&task));
    output_writable=false;
    assert(nas_epoll_wait_description(40,
               (uint64_t)(uintptr_t)output,1,0)==(uint64_t)-EFAULT);
    output_writable=true;
    assert(nas_epoll_wait_description(40,
               (uint64_t)(uintptr_t)output,1,0)==1);
    memcpy(&data,output+EP_DATA_OFFSET,8);
    assert(data==UINT64_C(0x12345678));
    nas_task_wait_dispose(&task);
    assert(!nas_open_files[40].refcount && nas_open_files[41].refcount==1);

    nas_open_files[41].refcount=UINT16_MAX;
    assert(nas_epoll_hold_instance(3,&slot)==-ENFILE);
    assert(nas_open_files[41].refcount==UINT16_MAX);
    nas_open_files[41].refcount=1;
    task.fds->entries[3]=(struct nas_fd_entry){0};
    nas_fs_drop_kernel_description(41);
    assert(!nas_open_files[41].refcount);
    puts("[M158 EPOLL WAIT PASS] real pinned-instance wake/completion, FD ABA, saturated hold and final cleanup");
    return 0;
}
