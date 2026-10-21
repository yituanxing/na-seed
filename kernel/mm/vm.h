#ifndef NASEED_MM_VM_H
#define NASEED_MM_VM_H
#include <stdint.h>
#include "../arch/mmu.h"
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                          uint64_t prot, uint64_t flags,
                          uint64_t fd, uint64_t offset);
uint64_t nas_vm_munmap(uint64_t addr, uint64_t length);
uint64_t nas_vm_brk(uint64_t want);
uint64_t nas_vm_mprotect(uint64_t addr, uint64_t length, uint64_t prot);
#endif
