#include "clock.h"
#include "../arch/arch.h"

/* Linux clocksource/timekeeper split, intentionally single-CPU for now.
 * A Limine RTC calendar sample anchors UTC once; runtime never reads RTC
 * for durations or scheduling. Absent/implausible RTC stays unavailable.
 * No adjustable wall clock, NTP, suspend/resume or leap-second support. */
static uint64_t mono_anchor_ns;
static uint64_t realtime_anchor_ns;
static bool realtime_ready;

void nas_timekeeper_init(int64_t boot_epoch_seconds) {
    realtime_ready=false;
    /* Firmware date must be plausible, within the representable ns horizon.
     * Reject zero, negative and obviously uninitialized RTC values. */
    if(boot_epoch_seconds < INT64_C(946684800) ||
       boot_epoch_seconds > INT64_C(4102444800)) return;
    mono_anchor_ns=nas_arch_monotonic_ns();
    realtime_anchor_ns=(uint64_t)boot_epoch_seconds*NAS_NSEC_PER_SEC;
    realtime_ready=true;
}

bool nas_time_realtime_available(void) {
    return realtime_ready;
}

bool nas_time_realtime_at(uint64_t mono,uint64_t *out) {
    if(!realtime_ready || !out || mono<mono_anchor_ns) return false;
    uint64_t delta=mono-mono_anchor_ns;
    if(delta>UINT64_MAX-realtime_anchor_ns) return false;
    *out=realtime_anchor_ns+delta;
    return true;
}

bool nas_time_realtime_ns(uint64_t *out) {
    return nas_time_realtime_at(nas_arch_monotonic_ns(),out);
}
