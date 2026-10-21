#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "elf.h"
extern void nas_print(const char *);
extern void nas_hex(uint64_t);
#define TASK (*nas_current_task)
#define heap_break TASK.heap_break
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define USER_STACK_TOP 0x0000000070000000ULL

/* Parse the ELF64 program headers rather than executing embedded kernel code. */
static bool checked_range(uint64_t start, uint64_t size, uint64_t limit) {
    return start <= limit && size <= limit - start;
}
/* Current fixed ET_DYN placement, not architecture-neutral ASLR. */
#define ALPINE_APP_BASE 0x10000000ULL
#define ALPINE_LDSO_BASE 0x30000000ULL

/* ELF bytes may originate from a Limine ELF module (M1-M4) or a file
 * inside the indexed CPIO initramfs (M5.1). This is not an execve ABI.
 * PT_INTERP is still checked against the one supported musl interpreter.
 */

static bool elf_map_image(const struct elf_image *image,
                          uint64_t bias, bool main_image,
                          uint64_t *entry, bool *has_interp) {
    if (!image || !image->data || image->size < sizeof(struct elf_hdr))
        return false;
    const uint8_t *file = image->data;
    const struct elf_hdr *e = (const struct elf_hdr *)file;
    if (e->ident[0] != 0x7f || e->ident[1] != 'E' ||
        e->ident[2] != 'L' || e->ident[3] != 'F' ||
        e->ident[4] != 2 || e->ident[5] != 1 ||
        (e->type != 2 && e->type != 3) || e->machine != 62 ||
        e->phentsize != sizeof(struct elf_phdr) ||
        !e->phnum || e->phnum > 64 ||
        !checked_range(e->phoff, (uint64_t)e->phnum * e->phentsize,
                       image->size))
        return false;
    if ((e->type == 2 && bias) || (e->type == 3 && !bias))
        return false;
    bool has_entry = false;
    const struct elf_phdr *ph = (const struct elf_phdr *)(file + e->phoff);
    for (unsigned i = 0; i < e->phnum; i++) {
        const struct elf_phdr *p = &ph[i];
        if (p->type == 3 && main_image) { /* PT_INTERP */
            static const char name[] = "/lib/ld-musl-x86_64.so.1";
            if (!checked_range(p->offset, p->filesz, image->size) ||
                p->filesz != sizeof(name)) return false;
            for (unsigned n = 0; n < sizeof(name); n++)
                if (file[p->offset + n] != (uint8_t)name[n]) return false;
            *has_interp = true;
        }
        if (p->type != 1) continue;
        if (!p->memsz || p->memsz > (64ULL << 20) ||
            p->filesz > p->memsz ||
            !checked_range(p->offset, p->filesz, image->size) ||
            !checked_range(p->vaddr, p->memsz, USER_LIMIT) ||
            p->vaddr > UINT64_MAX - bias ||
            p->memsz > UINT64_MAX - bias - p->vaddr)
            return false;
        uint64_t start = p->vaddr + bias, end = start + p->memsz;
        if (start < 0x10000 || end > USER_STACK_TOP - 0x100000 ||
            end > UINT64_MAX - PAGE) return false;
        if ((p->flags & 1) && e->entry >= p->vaddr &&
            e->entry - p->vaddr < p->memsz) has_entry = true;
        if (main_image && e->phoff >= p->offset &&
            e->phoff - p->offset < p->filesz &&
            (uint64_t)e->phnum * e->phentsize <=
                p->filesz - (e->phoff - p->offset))
            loaded_phdr = start + (e->phoff - p->offset);
        if (main_image && end > heap_break) heap_break = end;
        uint64_t first = start & ~(PAGE - 1);
        uint64_t last = (end + PAGE - 1) & ~(PAGE - 1);
        for (uint64_t va = first; va < last; va += PAGE)
            if (!nas_mmu_map_page(va, (p->flags & 2) != 0)) return false;
        if (!nas_user_copy_to_mapped(start, file + p->offset, p->filesz)) return false;
    }
    if (!has_entry || e->entry > UINT64_MAX - bias) return false;
    *entry = e->entry + bias;
    if (main_image) loaded_phnum = e->phnum;
    return true;
}
bool nas_elf_load(const struct elf_image *image,
                     const struct elf_image *interpreter, uint64_t *entry) {
    if (!image || !image->data || image->size < sizeof(struct elf_hdr))
        return false;
    const struct elf_hdr *e = (const struct elf_hdr *)image->data;
    uint64_t bias = e->type == 3 ? ALPINE_APP_BASE : 0;
    bool has_interp = false;
    if (!elf_map_image(image, bias, true, &loaded_main_entry, &has_interp))
        return false;
    *entry = loaded_main_entry;
    if (!has_interp) return true;
    if (!interpreter || !interpreter->data) return false;
    loaded_interp_base = ALPINE_LDSO_BASE;
    bool nested_interp = false;
    if (!elf_map_image(interpreter, loaded_interp_base, false, entry, &nested_interp))
        return false;
    nas_print("[DYN] real musl interpreter loaded, entry=");
    nas_hex(*entry); nas_print("\n");
    return true;
}
