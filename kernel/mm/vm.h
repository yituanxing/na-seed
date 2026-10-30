#ifndef NASEED_MM_VM_H
#define NASEED_MM_VM_H
#include <stdint.h>
#include "../arch/mmu.h"
#include "anon_regions.h"
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                          uint64_t prot, uint64_t flags,
                          uint64_t fd, uint64_t offset);
/* Eager, private file-backed mmap. MAP_FIXED is accepted only within an
 * existing owned VMA (the dynamic loader's already reserved region). */
uint64_t nas_vm_file_mmap(uint64_t hint, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset);
uint64_t nas_vm_munmap(uint64_t addr, uint64_t length);
uint64_t nas_vm_madvise(uint64_t addr, uint64_t length, uint64_t advice);
uint64_t nas_vm_brk(uint64_t want);
uint64_t nas_vm_mprotect(uint64_t addr, uint64_t length, uint64_t prot);
struct nas_task;
/* Logical VMA enumeration includes reserved PROT_NONE and nonresident pages.
 * Callers must check target Task lifetime/authorization first. */
bool nas_vm_walk_task_vmas(const struct nas_task *,nas_vma_visit,void *ctx);
/* Low-level resident PTE traversal retained for diagnostics only. */
bool nas_vm_walk_task_pages(const struct nas_task *task,
                             nas_mmu_user_visit callback,void *ctx);
/* INTERNAL, unprivileged callers forbidden until ptrace authorization.
 * Returns bytes copied, -EFAULT with zero progress, -ESRCH for dead Task. */
int64_t nas_vm_read_task_bytes(const struct nas_task *task,uint64_t va,
                               uint8_t *dst,uint64_t length);
/* Internal only: caller must prove ptrace authorization + STOPPED state.
 * A full preflight is performed before mutating any user page. */
int64_t nas_vm_write_task_bytes(const struct nas_task *task,uint64_t va,
                                const uint8_t *src,uint64_t length);
#endif
