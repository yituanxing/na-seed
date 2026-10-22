#ifndef NASEED_VM_PROT_H
#define NASEED_VM_PROT_H
#include <stdint.h>
/* Linux mmap/mprotect PROT_*, not native x86/ARM/RISC-V PTE flags.
 * Our first eager-mapping VM requires a present readable page and forbids
 * W+X. A real PROT_NONE mapping requires VMA metadata and must not be
 * simulated with a readable page. Refuse it explicitly until supported.
 */
#define NAS_VM_READ  1U
#define NAS_VM_WRITE 2U
#define NAS_VM_EXEC  4U
static inline int nas_vm_prot_supported(uint64_t prot) {
    return prot && !(prot & ~7ULL) &&
           !((prot & NAS_VM_WRITE) && (prot & NAS_VM_EXEC));
}
#endif
