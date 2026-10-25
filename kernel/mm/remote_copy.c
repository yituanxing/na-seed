/* Shared kernel-internal remote Task address read for future ptrace.
 * No userspace syscall and NO tracer access policy yet: do not expose.
 * Single CPU scheduler guarantees no concurrent VM destruction mid-read. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/mmu.h"
#include "frame.h"
#include "vm.h"
#include "../lib/string.h"
int64_t nas_vm_read_task_bytes(const struct nas_task *task,uint64_t va,
                               uint8_t *dst,uint64_t length) {
    if(!task || task->state==NAS_TASK_FREE || task->state==NAS_TASK_ZOMBIE)
        return -ESRCH;
    const struct nas_task *owner=task->vm_owner?task->vm_owner:task;
    if(!owner->root_phys)return -ESRCH;
    if(!length)return 0;
    if(!dst || va>=USER_LIMIT || length>USER_LIMIT-va)return -EFAULT;
    uint64_t count=0;
    while(count<length) {
        uint64_t at=va+count,pa=0;
        if(!nas_mmu_resolve_user_address(owner->root_phys,at,&pa,0))
            return count?(int64_t)count:-EFAULT;
        uint64_t room=4096ULL-(at&4095ULL);
        uint64_t n=length-count;
        if(n>room)n=room;
        memcpy(dst+count,(const void *)(uintptr_t)(nas_hhdm_offset()+pa),
               (size_t)n);
        count+=n;
    }
    return (int64_t)count;
}
