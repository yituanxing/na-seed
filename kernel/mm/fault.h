#ifndef NASEED_MM_FAULT_H
#define NASEED_MM_FAULT_H
#include <stdint.h>
struct nas_mm;
enum nas_mm_fault_result {
    NAS_MM_FAULT_RESOLVED=0, NAS_MM_FAULT_MAPERR, NAS_MM_FAULT_ACCERR,
    NAS_MM_FAULT_NEEDS_COW, NAS_MM_FAULT_NEEDS_BACKING,
    NAS_MM_FAULT_OOM, NAS_MM_FAULT_INVARIANT
};
/* Only MM resolves memory faults. No syscall, signal or ptrace dependencies. */
enum nas_mm_fault_result nas_mm_handle_fault(struct nas_mm *,uint64_t addr,
                                             unsigned access);
#endif
