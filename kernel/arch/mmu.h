#ifndef NASEED_ARCH_MMU_SELECT_H
#define NASEED_ARCH_MMU_SELECT_H
#include <stdint.h>
#include <stdbool.h>
/* COMMON kernel VM contract: NOT an ISA PTE format. VM, ELF, exec, FS,
 * usercopy and fork use identical operations. Implementations must provide
 * frame ownership, root switching and permission semantics, not fake stubs.
 * x86 keeps its existing proven page-tree provider. RV64/AArch64 use
 * mm/mmu_bridge.c over the shared user_space.c lifecycle and native PTEs.
 * All three link and execute the full kernel; this is not an M0 stub.
 */
#if defined(__x86_64__)
#include "x86_64/mmu.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "riscv64/mmu.h"
#elif defined(__aarch64__)
#include "aarch64/mmu.h"
#else
#error "NaSeed MMU architecture unavailable"
#endif
#ifndef PAGE
#define PAGE 4096ULL
#endif
#if !defined(__x86_64__)
uint64_t nas_arch_user_limit(void);
#define USER_LIMIT (nas_arch_user_limit())
bool nas_mmu_bootstrap(void);
uint64_t nas_mmu_alloc_root(void);
bool nas_mmu_reclaim_root(uint64_t);
void nas_mmu_switch_root(uint64_t);
uint64_t nas_mmu_fork_root(void);
bool nas_mmu_map_page(uint64_t, bool writable);
bool nas_mmu_unmap_page(uint64_t);
uint8_t *nas_mmu_user_byte(uint64_t);
bool nas_mmu_user_writable(uint64_t);
bool nas_mmu_protect_pages(uint64_t, uint64_t pages, uint64_t prot);
#endif
#endif
