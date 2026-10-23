#ifndef NASEED_FS_POSIX_LOCKS_H
#define NASEED_FS_POSIX_LOCKS_H
#include <stdbool.h>
#include <stdint.h>
/* Linux F_[GETLK|SETLK] record locks: exclusive end; UINT64_MAX means EOF.
 * Process ownership, not a shared file descriptor. Keep the lock state
 * in the actual tmpfs inode, not in a syscall or architecture adapter.
 * This bounded transactional table reports ENOLCK when truly exhausted. */
#define NAS_POSIX_LOCK_LIMIT 16
#define NAS_LOCK_READ 0U
#define NAS_LOCK_WRITE 1U
#define NAS_LOCK_UNLOCK 2U
struct nas_posix_lock {
    uint64_t pid, start, end;
    uint16_t type; /* 0 read, 1 write */
};
struct nas_posix_locks {
    struct nas_posix_lock entries[NAS_POSIX_LOCK_LIMIT];
    unsigned count;
};
static inline bool nas_lock_overlap(uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    return a<d && c<b;
}
static inline bool nas_lock_conflicting(const struct nas_posix_lock *a,
                  uint64_t pid,uint16_t type,uint64_t start,uint64_t end) {
    return a->pid!=pid && (a->type==NAS_LOCK_WRITE || type==NAS_LOCK_WRITE) &&
           nas_lock_overlap(a->start,a->end,start,end);
}
static inline int nas_locks_conflict(const struct nas_posix_locks *locks,
                uint64_t pid,uint16_t type,uint64_t start,uint64_t end) {
    for(unsigned i=0;i<locks->count;i++)
        if(nas_lock_conflicting(&locks->entries[i],pid,type,start,end))
            return (int)i;
    return -1;
}
static inline void nas_locks_release(struct nas_posix_locks *locks,uint64_t pid) {
    unsigned out=0;
    for(unsigned i=0;i<locks->count;i++)
        if(locks->entries[i].pid!=pid)locks->entries[out++]=locks->entries[i];
    locks->count=out;
}
/* Return 0 (committed), 1 (conflict), 2 (no slots), 3 (invalid).
 * Split overlapping locks belonging to the same owner, so SQLite can
 * release the pending byte without discarding its independent read lock. */
static inline int nas_locks_apply(struct nas_posix_locks *locks,uint64_t pid,
             uint16_t type,uint64_t start,uint64_t end) {
    if(!pid || type>NAS_LOCK_UNLOCK || start>=end) return 3;
    if(type!=NAS_LOCK_UNLOCK &&
       nas_locks_conflict(locks,pid,type,start,end)>=0) return 1;
    struct nas_posix_locks next={0};
    for(unsigned i=0;i<locks->count;i++) {
        const struct nas_posix_lock *a=&locks->entries[i];
        if(a->pid!=pid || !nas_lock_overlap(a->start,a->end,start,end)) {
            if(next.count==NAS_POSIX_LOCK_LIMIT)return 2;
            next.entries[next.count++]=*a;
            continue;
        }
        if(a->start<start) {
            if(next.count==NAS_POSIX_LOCK_LIMIT)return 2;
            struct nas_posix_lock left=*a;
            left.end=start;
            next.entries[next.count++]=left;
        }
        if(end<a->end) {
            if(next.count==NAS_POSIX_LOCK_LIMIT)return 2;
            struct nas_posix_lock right=*a;
            right.start=end;
            next.entries[next.count++]=right;
        }
    }
    if(type!=NAS_LOCK_UNLOCK) {
        if(next.count==NAS_POSIX_LOCK_LIMIT)return 2;
        next.entries[next.count++]=(struct nas_posix_lock){.pid=pid,
                            .start=start,.end=end,.type=type};
    }
    *locks=next;
    return 0;
}
#endif
