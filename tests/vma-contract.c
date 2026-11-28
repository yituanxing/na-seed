/* VMA ownership contract: compile the real kernel/mm/anon_regions.c. */
#include "../kernel/mm/anon_regions.h"
#include "../kernel/mm/device_backing.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE UINT64_C(4096)
struct nas_mapped_file { unsigned refs; bool refuse; };
bool nas_fs_mmap_get(struct nas_mapped_file *f) {
    if (f->refuse) return false;
    ++f->refs;
    return true;
}
void nas_fs_mmap_put(struct nas_mapped_file *f) {
    assert(f->refs);
    --f->refs;
}
void *nas_heap_alloc(size_t n) { return malloc(n); }
void *nas_heap_realloc(void *p,size_t n) { return realloc(p,n); }
bool nas_heap_free(void *p) { free(p); return true; }

struct device_state { unsigned refs; };
static bool device_get(void *ctx) {
    ++((struct device_state *)ctx)->refs;
    return true;
}
static void device_put(void *ctx) {
    struct device_state *s=ctx;
    assert(s->refs);
    --s->refs;
}
static int device_page(void *ctx,uint64_t off,uint64_t *pa) {
    (void)ctx;
    *pa=UINT64_C(0x2000)+off;
    return 0;
}
static unsigned count_vmas(const struct nas_anon_regions *r) {
    unsigned count=0;
    for (unsigned i=0;i<r->capacity;i++) count+=(r->slots[i].end!=0);
    return count;
}
static void gap_and_coalesce(void) {
    struct nas_anon_regions r={0};
    uint64_t addr=0;
    assert(nas_anon_find_gap(&r,PAGE,&addr) &&
           addr==NAS_ANON_CEILING-PAGE);
    assert(nas_vma_record(&r,addr,PAGE,3,NAS_VMA_ANON_PRIVATE,0));
    assert(nas_anon_find_gap(&r,PAGE,&addr) &&
           addr==NAS_ANON_CEILING-2*PAGE);
    assert(nas_vma_record(&r,addr,PAGE,3,NAS_VMA_ANON_PRIVATE,0));
    assert(count_vmas(&r)==1); /* adjacent identical anonymous policy */
    assert(!nas_vma_record(&r,addr,PAGE,3,NAS_VMA_ANON_PRIVATE,0));
    assert(!nas_anon_fixed_range_allowed(NAS_ANON_CEILING,PAGE));
    assert(!nas_anon_fixed_range_allowed(NAS_ANON_EXTENDED_FLOOR-1,PAGE));
    nas_anon_reset(&r);
    assert(nas_vma_record(&r,NAS_ANON_FLOOR,
                          NAS_ANON_CEILING-NAS_ANON_FLOOR,
                          3,NAS_VMA_ANON_PRIVATE,0));
    assert(nas_anon_find_gap(&r,PAGE,&addr) &&
           addr==NAS_ANON_EXTENDED_CEILING-PAGE);
    nas_anon_reset(&r);
}
static void backed_split_and_clone(void) {
    struct nas_mapped_file f={0};
    struct nas_anon_regions parent={0},child={0};
    uint64_t start=UINT64_C(0x40000000);
    assert(nas_vma_record_backed(&parent,start,3*PAGE,3,
                                 NAS_VMA_FILE_PRIVATE_LAZY,
                                 2*PAGE,&f));
    assert(f.refs==1);
    assert(nas_vma_clone(&child,&parent) && f.refs==2);
    assert(nas_vma_protect(&child,start+PAGE,PAGE,1));
    assert(count_vmas(&child)==3 && f.refs==4);
    const struct nas_anon_region *a=nas_vma_find(&child,start),
                                 *b=nas_vma_find(&child,start+PAGE),
                                 *c=nas_vma_find(&child,start+2*PAGE);
    assert(a && b && c && a->file_offset==2*PAGE &&
           b->file_offset==3*PAGE && c->file_offset==4*PAGE);
    assert(a->prot==3 && b->prot==1 && c->prot==3);
    assert(nas_vma_set_dontfork(&child,start+PAGE,PAGE,true));
    b=nas_vma_find(&child,start+PAGE);
    assert(b && (b->flags&NAS_VMA_DONTFORK));
    assert(nas_anon_forget(&child,start+PAGE,PAGE));
    assert(count_vmas(&child)==2 && f.refs==3);
    assert(nas_vma_find(&parent,start+PAGE)->prot==3);
    nas_anon_reset(&child);
    assert(f.refs==1);
    f.refuse=true;
    assert(!nas_vma_clone(&child,&parent));
    assert(!child.slots && !child.capacity && f.refs==1);
    f.refuse=false;
    nas_anon_reset(&parent);
    assert(f.refs==0);
}
static void device_lifetime(void) {
    struct device_state state={0};
    struct nas_vm_device device={
        .context=&state,.extent_bytes=4*PAGE,
        .get=device_get,.put=device_put,.page=device_page
    };
    struct nas_anon_regions original={0},copy={0};
    const uint64_t start=UINT64_C(0x30000000);
    assert(nas_vma_record_device(&original,start,3*PAGE,3,PAGE,&device));
    assert(state.refs==1);
    assert(nas_vma_clone(&copy,&original) && state.refs==2);
    assert(nas_anon_forget(&copy,start+PAGE,PAGE));
    assert(count_vmas(&copy)==2 && state.refs==3);
    assert(nas_vma_find(&copy,start+2*PAGE)->file_offset==3*PAGE);
    nas_anon_reset(&copy);
    assert(state.refs==1);
    nas_anon_reset(&original);
    assert(state.refs==0);
}
int main(void) {
    gap_and_coalesce();
    backed_split_and_clone();
    device_lifetime();
    puts("PASS: VMA gap/coalesce, backing offsets, clone, split, OOM rollback, device pins");
    return 0;
}
