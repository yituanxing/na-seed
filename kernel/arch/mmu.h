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
typedef bool (*nas_mmu_user_visit)(uint64_t va,unsigned access,void *ctx);
bool nas_mmu_walk_user_mapping(uint64_t root_phys,nas_mmu_user_visit visit,void *ctx);
/* A chosen VM owner (not the executing task). Output PA includes offset. */
bool nas_mmu_resolve_user_address(uint64_t selected_root,uint64_t user_va,
                                  uint64_t *physical,unsigned *access);
/* M86 transaction contract: prepare allocates any missing intermediate tables
 * without installing a user leaf. Exchange allocates nothing and transfers
 * physical-frame ownership to/from the PTE; caller releases old_pa.
 * The TLB is invalidated before a successful exchange returns.
 * For a PROT_NONE replacement pass new_pa=0 and prot=0. */
/* Explicit address-space identity for VM mutations. root_phys belongs to
 * the MM owner, NOT the currently installed CPU page-table register.
 * The caller retains physical-frame ownership until exchange succeeds.
 * Per-ISA backends handle required TLB invalidation. */
bool nas_mmu_prepare_leaf_at(uint64_t root_phys,uint64_t va);
bool nas_mmu_exchange_leaf_at(uint64_t root_phys,uint64_t va,
                              uint64_t new_pa,unsigned prot,
                              uint64_t *old_pa);
/* Transitional current-MM helpers for existing syscall/loader paths. */
bool nas_mmu_prepare_leaf(uint64_t va);
bool nas_mmu_exchange_leaf(uint64_t va,uint64_t new_pa,unsigned prot,
                           uint64_t *old_pa);
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
uint64_t nas_mmu_fork_root(uint64_t source_root);
bool nas_mmu_map_page(uint64_t, bool writable);
/* Typed user mapping permissions, never the ambiguous writable-only API. */
bool nas_mmu_map_prot_page(uint64_t va, uint64_t prot);
bool nas_mmu_unmap_page(uint64_t);
uint8_t *nas_mmu_user_byte(uint64_t);
bool nas_mmu_user_writable(uint64_t);
bool nas_mmu_protect_pages(uint64_t, uint64_t pages, uint64_t prot);
#endif
#endif
