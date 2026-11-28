#ifndef NASEED_TIME_COUNTER_H
#define NASEED_TIME_COUNTER_H
#include <stdint.h>
/* Caller validates nonzero hardware frequency; keep the existing split
 * quotient/remainder so tick*1e9 does not overflow at ordinary uptimes.
 * All three native providers retain their own calibration and resolution. */
static inline uint64_t nas_counter_ns(uint64_t ticks,uint64_t hz) {
    return (ticks/hz)*1000000000ULL+(ticks%hz)*1000000000ULL/hz;
}
#endif
