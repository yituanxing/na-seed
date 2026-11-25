#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../fd.h"
#include "../task.h"
#include "pidfd.h"

/* pid_t, not Task*: Task slots are reused after wait4. NaSeed's PID
 * allocator is monotonic (and rejects exhaustion), so a retained pidfd
 * cannot accidentally become a handle for the next occupant of a slot.
 * dup/fork share one File and its immutable target; final close reclaims it. */
static uint64_t targets[NAS_OPEN_FILE_LIMIT];

#define PIDFD_IN 0x0001U
#define PIDFD_RDNORM 0x0040U
#define PIDFD_HUP 0x0010U

uint64_t nas_pidfd_open(uint64_t raw_pid,uint64_t flags) {
    /* Linux pid_t is a signed 32-bit argument on all supported LP64 ISAs. */
    int32_t pid=(int32_t)(uint32_t)raw_pid;
    if(pid<=0 || flags)return (uint64_t)-EINVAL;
    if(!nas_task_by_pid((uint32_t)pid))return (uint64_t)-ESRCH;
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(nas_current_task,0,1,fds,slots,"pidfd_open");
    if(rc)return (uint64_t)(int64_t)rc;
    if(!nas_fd_install_new(nas_current_task,fds[0],slots[0],FD_PIDFD,
                           0,true))return (uint64_t)-EBADF;
    targets[slots[0]]=(uint32_t)pid;
    return fds[0];
}

uint16_t nas_pidfd_poll_description(unsigned slot,uint16_t events) {
    if(!nas_file_slot_live(slot) ||
       !targets[slot])return 0;
    const struct nas_task *target=nas_task_by_pid(targets[slot]);
    if(target && (target->state!=NAS_TASK_ZOMBIE ||
                  nas_task_group_has_live_peers(target)))return 0;
    /* Linux fs/pidfs.c: readable at zombie transition; after wait4 reaps
     * the task, readability persists and POLLHUP is additionally raised. */
    return (uint16_t)((events&PIDFD_IN ? PIDFD_IN|PIDFD_RDNORM : 0) |
                      (target ? 0 : PIDFD_HUP));
}


uint64_t nas_pidfd_read(unsigned slot,uint8_t *dst,uint64_t count) {
    (void)slot;(void)dst;(void)count;
    /* A pidfd reports readiness, but cannot be consumed via read(2). */
    return (uint64_t)-EINVAL;
}

void nas_pidfd_release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)targets[slot]=0;
}
