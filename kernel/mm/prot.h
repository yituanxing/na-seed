#ifndef NASEED_VM_PROT_H
#define NASEED_VM_PROT_H
#include <stdint.h>
/* Linux mmap/mprotect PROT_*, not native x86/ARM/RISC-V PTE flags.
 * PROT_NONE is a VMA-only reservation until explicitly made accessible.
 * Linux permits RWX mappings (notably the original V8 JIT CodeRange); the
 * three supported user PTE backends can express R|W|X simultaneously.
 * The VM layer remains responsible for ownership, lazy faults, and COW.
 */
#define NAS_VM_READ  1U
#define NAS_VM_WRITE 2U
#define NAS_VM_EXEC  4U
static inline int nas_vm_prot_supported(uint64_t prot) {
    return prot && !(prot & ~7ULL);
}
/* Internal leaf transitions have the same valid nonempty Linux protection
 * combinations; zero represents an absent rather than readable leaf. */
static inline int nas_pte_prot_supported(uint64_t prot) {
    return nas_vm_prot_supported(prot);
}
#endif
