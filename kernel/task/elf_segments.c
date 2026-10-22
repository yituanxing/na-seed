#include <stdint.h>
#include <stdbool.h>
#include "elf_segments.h"
#include "elf_format.h"
#define ELF_LOAD 1U
#define ELF_DYNAMIC 2U
#define ELF_INTERP 3U
#define PAGE 4096ULL
static bool checked(uint64_t start,uint64_t count,uint64_t size) {
    return start<=size && count<=size-start;
}
static bool same_text(const uint8_t *bytes,uint64_t size,const char *literal) {
    if (!literal) return false;
    uint64_t n=0;
    while(literal[n]) {
        if(n>=size || bytes[n]!=(uint8_t)literal[n]) return false;
        n++;
    }
    return n+1==size && bytes[n]==0;
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
bool nas_elf_segments_load(const struct elf_image *image,
    const struct nas_elf_segment_policy *opt,
    const struct nas_elf_segment_ops *ops,
    struct nas_elf_segment_result *out) {
    if (!image || !opt || !ops || !out ||
        !ops->map_page || !ops->copy_bytes || !image->data ||
        !opt->max_segment || opt->min_va>=opt->max_va)
        return false;
    struct nas_elf_view view;
    if (!nas_elf_open(&view,image->data,image->size,opt->machine,
                      opt->allow_dynamic_elf,opt->max_phnum))
        return false;
    const struct elf_hdr *h=view.header;
    const struct elf_phdr *ph=view.phdrs;
    if ((h->type==2 && opt->bias) || (h->type==3 && !opt->bias))
        return false;
    struct nas_elf_segment_result result={0};
    result.phnum=h->phnum;
    bool has_entry=false,has_load=false;
    uint64_t pages=0;
    /* Validate the WHOLE ELF before the first page allocation: not an
     * ad-hoc validation loop per instruction-set architecture. */
    for(unsigned i=0;i<h->phnum;i++) {
        const struct elf_phdr *p=&ph[i];
        if(p->type==ELF_INTERP) {
            if (!checked(p->offset,p->filesz,image->size) ||
                !same_text(image->data+p->offset,p->filesz,opt->interpreter))
                return false;
            result.has_interp=true;
        }
        if(p->type==ELF_DYNAMIC && opt->reject_pt_dynamic) return false;
        if(p->type!=ELF_LOAD) continue;
        uint64_t first,last;
        if(!segment_bounds(p,image->size,opt,&first,&last)) return false;
        if(opt->max_pages) {
            uint64_t n=(last-first)/PAGE;
            if(n>opt->max_pages-pages) return false;
            pages+=n;
        }
        if(opt->reject_overlapping_pages)
            for(unsigned j=0;j<i;j++) {
                const struct elf_phdr *q=&ph[j];
                if(q->type!=ELF_LOAD) continue;
                uint64_t qfirst,qlast;
                if(!segment_bounds(q,image->size,opt,&qfirst,&qlast))
                    return false;
                if(first<qlast && qfirst<last) return false;
            }
        if(p->flags & 1U) {
            if(h->entry>=p->vaddr && h->entry-p->vaddr<p->memsz)
                has_entry=true;
        }
        uint64_t end=p->vaddr+opt->bias+p->memsz;
        if(end>result.end) result.end=end;
        if(h->phoff>=p->offset && h->phoff-p->offset<p->filesz &&
           (uint64_t)h->phnum*h->phentsize<=
             p->filesz-(h->phoff-p->offset))
            result.phdr=p->vaddr+opt->bias+(h->phoff-p->offset);
        has_load=true;
    }
    if (!has_load || !has_entry || h->entry>UINT64_MAX-opt->bias)
        return false;
    result.entry=h->entry+opt->bias;
    /* Exactly one PT_LOAD mapper + BSS semantics. Backends select only
     * their PTE encoding and data access; zeroed allocated frames
     * supply BSS, same as existing full ELF/exec implementation. */
    for(unsigned i=0;i<h->phnum;i++) {
        const struct elf_phdr *p=&ph[i];
        if(p->type!=ELF_LOAD) continue;
        uint64_t first,last;
        if(!segment_bounds(p,image->size,opt,&first,&last)) return false;
        for(uint64_t va=first;va<last;va+=PAGE)
            if(!ops->map_page(ops->ctx,va,p->flags)) return false;
        if(p->filesz &&
           !ops->copy_bytes(ops->ctx,p->vaddr+opt->bias,
                            image->data+p->offset,p->filesz))
            return false;
    }
    *out=result;
    return true;
}
