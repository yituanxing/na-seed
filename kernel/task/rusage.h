#ifndef NASEED_LINUX_RUSAGE_H
#define NASEED_LINUX_RUSAGE_H
#include <stdint.h>
#include <stddef.h>

/* Native Linux LP64 wait4 rusage ABI, shared by x86-64/RV64/AArch64.
 * This is intentionally NOT a host struct rusage (libc may differ). */
struct nas_linux_timeval {
    int64_t sec,usec;
};
struct nas_linux_rusage {
    struct nas_linux_timeval utime,stime;
    int64_t maxrss,ixrss,idrss,isrss,minflt,majflt,nswap;
    int64_t inblock,oublock,msgsnd,msgrcv,nsignals,nvcsw,nivcsw;
};
_Static_assert(sizeof(struct nas_linux_rusage)==144,"Linux LP64 rusage");
_Static_assert(offsetof(struct nas_linux_rusage,maxrss)==32,"Linux maxrss");
_Static_assert(offsetof(struct nas_linux_rusage,nivcsw)==136,"Linux nivcsw");

static inline struct nas_linux_rusage nas_rusage_from_cpu_ns(uint64_t cpu_ns) {
    /* Existing scheduler measures real aggregate CPU occupancy but does
     * not yet distinguish userspace from kernel time. Publish the observed
     * aggregate as utime, leave unknown stime/counters zero, never invent
     * hard-coded timing samples or claim fabricated kernel usage. */
    struct nas_linux_rusage result={0};
    result.utime.sec=(int64_t)(cpu_ns/1000000000ULL);
    result.utime.usec=(int64_t)((cpu_ns%1000000000ULL)/1000ULL);
    return result;
}
#endif
