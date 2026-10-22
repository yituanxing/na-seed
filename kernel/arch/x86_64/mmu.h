#ifndef NASEED_X86_64_MMU_H
#define NASEED_X86_64_MMU_H
#include <stdbool.h>
#include <stdint.h>
#define PAGE 4096ULL
#define USER_LIMIT 0x0000800000000000ULL
bool nas_mmu_bootstrap(void);
uint64_t nas_mmu_alloc_root(void);
bool nas_mmu_reclaim_root(uint64_t root);
void nas_mmu_switch_root(uint64_t root);
uint64_t nas_mmu_fork_root(void);
bool nas_mmu_map_page(uint64_t addr, bool writable);
/* Typed Linux PROT_* for anonymous mmap; enforces NX from the FIRST PTE. */
bool nas_mmu_map_prot_page(uint64_t addr, uint64_t prot);
bool nas_mmu_unmap_page(uint64_t addr);
uint8_t *nas_mmu_user_byte(uint64_t address);
bool nas_mmu_user_writable(uint64_t address);
bool nas_mmu_protect_pages(uint64_t addr, uint64_t page_count, uint64_t prot);
#endif
