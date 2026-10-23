#ifndef NASEED_TASK_CPU_AFFINITY_H
#define NASEED_TASK_CPU_AFFINITY_H
#include <stdint.h>
/* Single-core scheduler: all three native QEMU guests expose real CPU0. */
#define NAS_CPU_AFFINITY_BYTES sizeof(uint64_t)
#define NAS_CPU_AFFINITY_MASK 1ULL
static inline int nas_cpu_affinity_valid_size(uint64_t size) {
    return size>=NAS_CPU_AFFINITY_BYTES;
}
static inline int nas_cpu_affinity_allowed(uint64_t mask) {
    return (mask & NAS_CPU_AFFINITY_MASK)!=0;
}
#endif
