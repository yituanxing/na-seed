#include <stdint.h>
#include <stdbool.h>
#include "../../task/elf_format.h"
#include "../abi.h"
#include "../../mm/frame.h"
#include "mmu.h"
#include "elf_image.h"

#define RV_ELF_MIN_VA 0x00010000ULL
#define RV_ELF_STACK_VA 0x00020000ULL
#define RV_MAX_USER_PAGES 32U
#define ELF_LOAD 1U
#define ELF_DYNAMIC 2U
#define ELF_INTERP 3U

static uint64_t max64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint64_t min64(uint64_t a, uint64_t b) { return a < b ? a : b; }

bool nas_rv_elf_load(struct nas_rv_page_table *pt,
                     const uint8_t *image, uint64_t size,
                     uint64_t *entry) {
    if (!pt || !pt->root_pa || !image || !entry || size < sizeof(struct elf_hdr) ||
        size > (1ULL << 20)) return false;

    struct nas_elf_view view;
    if (!nas_elf_open(&view, image, size, NAS_ELF_MACHINE, false, 16))
        return false;
    const struct elf_hdr *h = view.header;
    const struct elf_phdr *ph = view.phdrs;
    bool has_entry = false;
    unsigned total_pages = 0;

    /* Validate the COMPLETE image before allocating or modifying page tables.
     * In particular reject overlapping page-aligned LOAD ranges, W+X,
     * executable entry in non-X mappings and an image in the test stack. */
    for (unsigned i = 0; i < h->phnum; i++) {
        const struct elf_phdr *p = &ph[i];
        if (p->type == ELF_INTERP || p->type == ELF_DYNAMIC) return false;
        if (p->type != ELF_LOAD) continue;
        if (!p->memsz || p->filesz > p->memsz ||
            !nas_elf_segment_file_ok(p, size) ||
            (p->offset & 4095) != (p->vaddr & 4095) ||
            p->vaddr < RV_ELF_MIN_VA ||
            p->vaddr >= RV_ELF_STACK_VA ||
            p->memsz > RV_ELF_STACK_VA - p->vaddr ||
            (p->flags & ~7U) || !(p->flags & 4U) ||
            (p->flags & 3U) == 3U)
            return false;

        uint64_t lo = p->vaddr & ~4095ULL;
        uint64_t hi = (p->vaddr + p->memsz + 4095) & ~4095ULL;
        total_pages += (unsigned)((hi - lo) / 4096);
        if (total_pages > RV_MAX_USER_PAGES) return false;
        if ((p->flags & 1U) && h->entry >= p->vaddr &&
            h->entry - p->vaddr < p->memsz)
            has_entry = true;
        for (unsigned j = 0; j < i; j++) {
            const struct elf_phdr *q = &ph[j];
            if (q->type != ELF_LOAD) continue;
            uint64_t qlo = q->vaddr & ~4095ULL;
            uint64_t qhi = (q->vaddr + q->memsz + 4095) & ~4095ULL;
            if (lo < qhi && qlo < hi) return false;
        }
    }
    if (!has_entry || !total_pages) return false;

    uint64_t mapped_va[RV_MAX_USER_PAGES];
    uint64_t mapped_pa[RV_MAX_USER_PAGES];
    unsigned mapped = 0;
    uint64_t hhdm = nas_hhdm_offset();
    for (unsigned i = 0; i < h->phnum; i++) {
        const struct elf_phdr *p = &ph[i];
        if (p->type != ELF_LOAD) continue;
        uint64_t permissions = NAS_RV_PTE_U | NAS_RV_PTE_R;
        if (p->flags & 2U) permissions |= NAS_RV_PTE_W;
        if (p->flags & 1U) permissions |= NAS_RV_PTE_X;
        const uint64_t first = p->vaddr & ~4095ULL;
        const uint64_t last = (p->vaddr + p->memsz + 4095) & ~4095ULL;
        for (uint64_t va = first; va < last; va += NAS_RV_PAGE_SIZE) {
            uint64_t pa = nas_frame_alloc();
            if (!pa) goto rollback;
            if (!nas_rv_pt_map(pt, va, pa, permissions)) {
                (void)nas_frame_free(pa);
                goto rollback;
            }
            mapped_va[mapped] = va;
            mapped_pa[mapped] = pa;
            mapped++;
            /* nas_frame_alloc zeroes 4096 bytes, including ELF .bss.
             * Copy only the file-backed overlap with this particular page. */
            uint64_t copy_begin = max64(va, p->vaddr);
            uint64_t file_end = p->vaddr + p->filesz;
            uint64_t copy_end = min64(va + NAS_RV_PAGE_SIZE, file_end);
            if (copy_end > copy_begin) {
                uint8_t *dst = (uint8_t *)(uintptr_t)(hhdm + pa);
                const uint8_t *src = image + p->offset + (copy_begin - p->vaddr);
                for (uint64_t j = 0; j < copy_end - copy_begin; j++)
                    dst[copy_begin - va + j] = src[j];
            }
        }
    }
    *entry = h->entry;
    return true;

rollback:
    while (mapped) {
        mapped--;
        (void)nas_rv_pt_unmap(pt, mapped_va[mapped]);
        (void)nas_frame_free(mapped_pa[mapped]);
    }
    return false;
}
