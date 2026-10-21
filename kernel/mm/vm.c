#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../arch/arch.h"
#include "vm.h"
#include "prot.h"
#define TASK (*nas_current_task)
#define heap_break TASK.heap_break
#define mmap_cursor TASK.mmap_cursor
/* M2: genuine Linux ABI semantics for single-process TLS + anonymous VM. */
uint64_t nas_vm_anon_mmap(uint64_t hint, uint64_t length,
                              uint64_t prot, uint64_t flags,
                              uint64_t fd, uint64_t offset) {
    (void)hint;
    /* MAP_PRIVATE|MAP_ANONYMOUS; no file/offset/map-fixed emulation. */
    if (!length || length > (64ULL << 20) || length > UINT64_MAX - PAGE)
        return (uint64_t)-EINVAL;
    if ((flags & 0x22) != 0x22 || (flags & ~0x22ULL) || fd != UINT64_MAX || offset)
        return (uint64_t)-ENOSYS;
    if (prot & ~7ULL) return (uint64_t)-EINVAL;
    if (!prot) return (uint64_t)-ENOSYS; /* needs VMA, not a readable page */
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    if (!nas_arch_user_nx_supported()) return (uint64_t)-ENOSYS;
    uint64_t count = (length + PAGE - 1) / PAGE;
    if (count > mmap_cursor / PAGE) return (uint64_t)-ENOMEM;
    uint64_t start = mmap_cursor - count * PAGE;
    if (start < 0x20000000 || start >= USER_LIMIT) return (uint64_t)-ENOMEM;
    uint64_t va = start;
    for (; va < mmap_cursor; va += PAGE)
        if (!nas_mmu_map_prot_page(va, prot)) break;
    if (va < mmap_cursor) {
        while (va > start) { va -= PAGE; nas_mmu_unmap_page(va); }
        return (uint64_t)-ENOMEM;
    }
    mmap_cursor = start;
    return start;
}
uint64_t nas_vm_munmap(uint64_t addr, uint64_t len) {
    if (!len || (addr & (PAGE - 1)) || len > UINT64_MAX - (PAGE - 1))
        return (uint64_t)-EINVAL;
    uint64_t count = (len + PAGE - 1) / PAGE;
    if (addr >= USER_LIMIT || count > (USER_LIMIT - addr) / PAGE)
        return (uint64_t)-EINVAL;
    for (uint64_t i = 0; i < count; i++)
        if (!nas_mmu_unmap_page(addr + i * PAGE)) return (uint64_t)-EFAULT;
    return 0;
}
uint64_t nas_vm_brk(uint64_t want) {
    uint64_t base = heap_break;
    if (!want) return heap_break;
    /* brk is a Linux special: failure returns the unchanged break. */
    if (want < 0x10000 || want >= 0x20000000) return base;
    uint64_t before = (heap_break + PAGE - 1) & ~(PAGE - 1);
    uint64_t after = (want + PAGE - 1) & ~(PAGE - 1);
    if (after > before) {
        uint64_t va = before;
        for (; va < after; va += PAGE)
            if (!nas_mmu_map_page(va, true)) break;
        if (va != after) {
            while (va > before) { va -= PAGE; nas_mmu_unmap_page(va); }
            return base;
        }
    } else if (after < before) {
        for (uint64_t va = after; va < before; va += PAGE)
            if (!nas_mmu_unmap_page(va)) return base;
    }
    heap_break = want;
    return want;
}

/* VM protection semantics are shared; arch provider reports executable-disable enforcement. */
uint64_t nas_vm_mprotect(uint64_t addr, uint64_t length, uint64_t prot) {
    if (addr & (PAGE - 1) || (prot & ~7ULL) ||
        length > UINT64_MAX - (PAGE - 1))
        return (uint64_t)-EINVAL;
    if (!length) return 0;
    uint64_t pages = (length + PAGE - 1) / PAGE;
    if (addr >= USER_LIMIT || pages > (USER_LIMIT - addr) / PAGE)
        return (uint64_t)-ENOMEM;
    if (!nas_arch_user_nx_supported()) {
        /* Cannot claim executable permission enforcement without NXE. */
        return (uint64_t)-ENOSYS;
    }
    if (!prot) return (uint64_t)-ENOSYS; /* PROT_NONE needs VMA semantics. */
    if (!nas_vm_prot_supported(prot)) return (uint64_t)-EOPNOTSUPP;
    bool ok=nas_mmu_protect_pages(addr, pages, prot);
    return ok ? 0 : (uint64_t)-ENOMEM;
}
