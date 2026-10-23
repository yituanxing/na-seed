#include <stdint.h>
#include <stdbool.h>
#include "elf_segments.h"
#include "elf_format.h"
#include "../lib/string.h"
#define ELF_LOAD 1U
#define ELF_DYNAMIC 2U
#define ELF_INTERP 3U
#define PAGE 4096ULL
static bool checked(uint64_t start,uint64_t count,uint64_t size) {
    return start<=size && count<=size-start;
}
static bool same_text(const uint8_t *bytes,uint64_t size,const char *literal) {
    if (!literal) return false;
    size_t n = strlen(literal);
    /* PT_INTERP is a bounded binary segment including exactly one NUL.
     * Never invoke strcmp on ELF bytes, which are not independently
     * guaranteed to be a terminated string. */
    return n < size && size - n == 1 &&
           bytes[n] == 0 && memcmp(bytes, literal, n) == 0;
}
static bool segment_bounds(const struct elf_phdr *p, uint64_t image_size,
                          const struct nas_elf_segment_policy *opt,
                          uint64_t *first,uint64_t *last) {
    if (!p->memsz || p->memsz>opt->max_segment ||
        !nas_elf_segment_file_ok(p,image_size) ||
        (p->flags & ~7U) ||
        (p->flags & 4U)==0 ||
        (opt->forbid_wx && (p->flags & 3U)==3U) ||
        (opt->require_page_congruence &&
         (p->offset & (PAGE-1)) != (p->vaddr & (PAGE-1))) ||
        p->vaddr > UINT64_MAX-opt->bias)
        return false;
    uint64_t begin=p->vaddr+opt->bias;
    if(begin<opt->min_va || begin>=opt->max_va ||
       p->memsz>opt->max_va-begin ||
       begin>UINT64_MAX-p->memsz-(PAGE-1))
        return false;
    *first=begin&~(PAGE-1);
    *last=(begin+p->memsz+PAGE-1)&~(PAGE-1);
    return true;
}
/* A contiguous, read-only CPIO source remains zero-copy. A writable
 * tmpfs inode supplies bounded read_at operations instead; neither the
 * ELF layer nor an ISA backend needs to understand tmpfs page tables. */
bool nas_elf_image_read(const struct elf_image *image, uint64_t offset,
                        void *destination, uint64_t length) {
    if (!image || !destination || offset > image->size ||
        length > image->size - offset) return false;
    if (!length) return true;
    if (image->data) {
        memcpy(destination, image->data + offset, (size_t)length);
        return true;
    }
    return image->read_at &&
           image->read_at(image->context, offset, (uint8_t *)destination, length);
}
bool nas_elf_image_open(const struct elf_image *image, uint16_t machine,
                        bool allow_dyn, unsigned max_phnum,
                        struct elf_hdr *header) {
    return image && header &&
        nas_elf_image_read(image, 0, header, sizeof(*header)) &&
        nas_elf_header_valid(header, image->size, machine, allow_dyn, max_phnum);
}
static bool image_phdr(const struct elf_image *image, const struct elf_hdr *h,
                       unsigned index, struct elf_phdr *out) {
    if (index >= h->phnum) return false;
    return nas_elf_image_read(image,
        h->phoff + (uint64_t)index * sizeof(*out), out, sizeof(*out));
}
bool nas_elf_segments_load(const struct elf_image *image,
    const struct nas_elf_segment_policy *opt,
    const struct nas_elf_segment_ops *ops,
    struct nas_elf_segment_result *out) {
    if (!image || !opt || !ops || !out ||
        !ops->map_page || !ops->copy_bytes ||
        (!image->data && !image->read_at) ||
        !opt->max_segment || opt->min_va >= opt->max_va)
        return false;
    struct elf_hdr h;
    if (!nas_elf_image_open(image, opt->machine, opt->allow_dynamic_elf,
                            opt->max_phnum, &h)) return false;
    if ((h.type == 2 && opt->bias) || (h.type == 3 && !opt->bias))
        return false;
    struct nas_elf_segment_result result = {0};
    result.phnum = h.phnum;
    bool has_entry = false, has_load = false;
    uint64_t pages = 0;
    /* Validate every header before mutating any page table. This handles
     * the same malformed-ELF cases for CPIO and sparse tmpfs sources. */
    for (unsigned i = 0; i < h.phnum; i++) {
        struct elf_phdr ph;
        if (!image_phdr(image, &h, i, &ph)) return false;
        const struct elf_phdr *p = &ph;
        if (p->type == ELF_INTERP) {
            uint8_t name[256];
            if (p->filesz > sizeof(name) ||
                !checked(p->offset, p->filesz, image->size) ||
                !nas_elf_image_read(image, p->offset, name, p->filesz) ||
                !same_text(name, p->filesz, opt->interpreter))
                return false;
            result.has_interp = true;
        }
        if (p->type == ELF_DYNAMIC && opt->reject_pt_dynamic) return false;
        if (p->type != ELF_LOAD) continue;
        uint64_t first, last;
        if (!segment_bounds(p, image->size, opt, &first, &last)) return false;
        if (opt->max_pages) {
            uint64_t n = (last - first) / PAGE;
            if (n > opt->max_pages - pages) return false;
            pages += n;
        }
        if (opt->reject_overlapping_pages)
            for (unsigned j = 0; j < i; j++) {
                struct elf_phdr prior;
                if (!image_phdr(image, &h, j, &prior)) return false;
                if (prior.type != ELF_LOAD) continue;
                uint64_t qfirst, qlast;
                if (!segment_bounds(&prior, image->size, opt, &qfirst, &qlast))
                    return false;
                if (first < qlast && qfirst < last) return false;
            }
        if ((p->flags & 1U) &&
            h.entry >= p->vaddr && h.entry - p->vaddr < p->memsz)
            has_entry = true;
        uint64_t end = p->vaddr + opt->bias + p->memsz;
        if (end > result.end) result.end = end;
        if (h.phoff >= p->offset && h.phoff - p->offset < p->filesz &&
            (uint64_t)h.phnum * h.phentsize <=
             p->filesz - (h.phoff - p->offset))
            result.phdr = p->vaddr + opt->bias + (h.phoff - p->offset);
        has_load = true;
    }
    if (!has_load || !has_entry || h.entry > UINT64_MAX - opt->bias)
        return false;
    result.entry = h.entry + opt->bias;
    /* The architecture still provides only map/copy callbacks. For tmpfs
     * we stream file bytes in bounded blocks; ELF does not require a
     * physically or virtually contiguous copy of the whole executable. */
    for (unsigned i = 0; i < h.phnum; i++) {
        struct elf_phdr ph;
        if (!image_phdr(image, &h, i, &ph)) return false;
        const struct elf_phdr *p = &ph;
        if (p->type != ELF_LOAD) continue;
        uint64_t first, last;
        if (!segment_bounds(p, image->size, opt, &first, &last)) return false;
        for (uint64_t va = first; va < last; va += PAGE)
            if (!ops->map_page(ops->ctx, va, p->flags)) return false;
        if (p->filesz) {
            if (image->data) {
                if (!ops->copy_bytes(ops->ctx, p->vaddr + opt->bias,
                                     image->data + p->offset, p->filesz))
                    return false;
            } else {
                uint8_t chunk[1024];
                uint64_t copied = 0;
                while (copied < p->filesz) {
                    uint64_t n = p->filesz - copied;
                    if (n > sizeof(chunk)) n = sizeof(chunk);
                    if (!nas_elf_image_read(image, p->offset + copied, chunk, n) ||
                        !ops->copy_bytes(ops->ctx,
                            p->vaddr + opt->bias + copied, chunk, n))
                        return false;
                    copied += n;
                }
            }
        }
    }
    *out = result;
    return true;
}
