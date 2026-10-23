#ifndef NASEED_TIME_CLOCK_H
#define NASEED_TIME_CLOCK_H
#include <stdint.h>
#include <stdbool.h>
/* Common Linux timespec64 arithmetic. A duration always fits in uint64 ns
 * before it is added to the monotonic hardware clock. No fake wall clock. */
#define NAS_NSEC_PER_SEC UINT64_C(1000000000)
/* UNIX RTC epoch is optional. Monotonic time does not depend on it. */
void nas_timekeeper_init(int64_t boot_epoch_seconds);
bool nas_time_realtime_available(void);
bool nas_time_realtime_at(uint64_t monotonic_ns, uint64_t *realtime_ns);
bool nas_time_realtime_ns(uint64_t *realtime_ns);
static inline bool nas_time_duration_ns(uint64_t sec,uint64_t nsec,uint64_t *ns) {
    if ((int64_t)sec < 0 || nsec >= NAS_NSEC_PER_SEC ||
        sec > UINT64_MAX / NAS_NSEC_PER_SEC) return false;
    uint64_t whole=sec*NAS_NSEC_PER_SEC;
    if (whole > UINT64_MAX - nsec) return false;
    *ns=whole+nsec;
    return true;
}
static inline uint64_t nas_time_add_deadline(uint64_t now,uint64_t duration) {
    return duration > UINT64_MAX - now ? UINT64_MAX : now+duration;
}
static inline bool nas_time_deadline_expired(uint64_t now,uint64_t deadline) {
    return now >= deadline;
}
#endif
