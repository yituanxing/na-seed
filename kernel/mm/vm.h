#ifndef NASEED_MM_VM_H
#define NASEED_MM_VM_H
#include <stdint.h>
#include "../arch/mmu.h"
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                          uint64_t prot, uint64_t flags,
                          uint64_t fd, uint64_t offset);
/* Eager, private file-backed mmap. MAP_FIXED is accepted only within an
 * existing owned VMA (the dynamic loader's already reserved region). */
uint64_t nas_vm_file_mmap(uint64_t hint, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset);
uint64_t nas_vm_munmap(uint64_t addr, uint64_t length);
uint64_t nas_vm_brk(uint64_t want);
uint64_t nas_vm_mprotect(uint64_t addr, uint64_t length, uint64_t prot);
struct nas_task;
/* Actual present user PTEs from selected task: no fabricated permissions. */
bool nas_vm_walk_task_pages(const struct nas_task *task,
                             nas_mmu_user_visit callback,void *ctx);
#endif
