#include "anon_regions.h"
#include "heap.h"
#include "../lib/string.h"
#include <stddef.h>
#define PAGE 4096ULL
static bool aligned(uint64_t n) { return (n&(PAGE-1))==0; }
static bool valid_interval(uint64_t start,uint64_t length) {
    return length && aligned(start) && aligned(length) &&
           start>=NAS_ANON_FIXED_FLOOR && start<NAS_VMA_ADDRESS_LIMIT &&
           length<=NAS_VMA_ADDRESS_LIMIT-start;
}
void nas_anon_reset(struct nas_anon_regions *regions) {
    if (!regions) return;
    if (regions->slots) (void)nas_heap_free(regions->slots);
    *regions=(struct nas_anon_regions){0};
}
bool nas_vma_clone(struct nas_anon_regions *dst,
                   const struct nas_anon_regions *src) {
    if(!dst || !src)return false;
    if(dst==src)return true;
    struct nas_anon_regions copy={0};
    if(src->capacity) {
        if(!src->slots)return false;
        copy.slots=nas_heap_alloc((size_t)src->capacity*sizeof(*src->slots));
        if(!copy.slots)return false;
        memcpy(copy.slots,src->slots,(size_t)src->capacity*sizeof(*src->slots));
        copy.capacity=src->capacity;
    }
    nas_vma_commit(dst,&copy);
    return true;
}
void nas_vma_commit(struct nas_anon_regions *dst,
                    struct nas_anon_regions *staged) {
    if(!dst || !staged || dst==staged)return;
    nas_anon_reset(dst);
    *dst=*staged;
    *staged=(struct nas_anon_regions){0};
}
static bool grow_regions(struct nas_anon_regions *r) {
    unsigned next=r->capacity ? r->capacity*2U : NAS_ANON_REGION_INITIAL_CAPACITY;
    if(next<=r->capacity)
        return false;
    struct nas_anon_region *slots=nas_heap_realloc(r->slots,
                                                   (size_t)next*sizeof(*slots));
    if(!slots)return false;
    memset(slots+r->capacity,0,
           (size_t)(next-r->capacity)*sizeof(*slots));
    r->slots=slots;
    r->capacity=next;
    return true;
}
static int empty_slot(const struct nas_anon_regions *regions) {
    for (unsigned i=0;i<regions->capacity;++i)
        if (!regions->slots[i].end) return (int)i;
    return -1;
}
bool nas_anon_find_gap(const struct nas_anon_regions *regions,
                       uint64_t length,uint64_t *addr) {
    if (!regions || !addr || !length || !aligned(length) ||
        length>NAS_ANON_CEILING-NAS_ANON_FLOOR) return false;
    uint64_t top=NAS_ANON_CEILING;
    for (unsigned round=0;round<=regions->capacity;++round) {
        if (top<NAS_ANON_FLOOR+length) return false;
        uint64_t start=top-length, next_top=top;
        bool collided=false;
        for (unsigned i=0;i<regions->capacity;++i) {
            const struct nas_anon_region *r=&regions->slots[i];
            if (r->end && start<r->end && r->start<top) {
                if (!collided || r->start>next_top) next_top=r->start;
                collided=true;
            }
        }
        if (!collided) { *addr=start; return true; }
        if (next_top>=top) return false;
        top=next_top;
    }
    return false;
}
/* Coalesce only matching logical policies. File offsets must be contiguous.
 * This prevents RW and RX mappings from erasing each other's VMA semantics. */
static bool compatible(const struct nas_anon_region *a,
                       const struct nas_anon_region *b) {
    if(a->prot!=b->prot || a->kind!=b->kind)return false;
    /* A private eager file snapshot does NOT carry a durable backing ref.
     * Never coalesce independent snapshots merely due to adjacent offsets:
     * the FD may have been closed/reused for another source. */
    if(a->kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT ||
       a->kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT)return false;
    return true;
}
static bool insert_area(struct nas_anon_regions *regions,
                        struct nas_anon_region candidate) {
    if(!regions || candidate.end<=candidate.start ||
       !valid_interval(candidate.start,candidate.end-candidate.start) ||
       (candidate.prot &~7U) || candidate.kind>NAS_VMA_SIGNAL_TRAMPOLINE)
        return false;
    int left=-1,right=-1;
    for(unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *r=&regions->slots[i];
        if(!r->end)continue;
        if(candidate.start<r->end && r->start<candidate.end)return false;
        if(r->end==candidate.start && compatible(r,&candidate))left=(int)i;
        if(r->start==candidate.end && compatible(&candidate,r))right=(int)i;
    }
    if(left>=0 && right>=0 && left!=right) {
        regions->slots[left].end=regions->slots[right].end;
        regions->slots[right]=(struct nas_anon_region){0};
    } else if(left>=0)regions->slots[left].end=candidate.end;
    else if(right>=0) {
        regions->slots[right].start=candidate.start;
        regions->slots[right].file_offset=candidate.file_offset;
    } else {
        int slot=empty_slot(regions);
        if(slot<0) {
            if(!grow_regions(regions))return false;
            slot=empty_slot(regions);
        }
        if(slot<0)return false;
        regions->slots[slot]=candidate;
    }
    return true;
}
bool nas_vma_record(struct nas_anon_regions *regions,uint64_t addr,uint64_t length,
                    unsigned prot,unsigned kind,uint64_t file_offset) {
    if(!valid_interval(addr,length) ||
       ((kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT ||
         kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT) &&
        length>UINT64_MAX-file_offset))return false;
    return insert_area(regions,(struct nas_anon_region){
        .start=addr,.end=addr+length,.file_offset=file_offset,
        .prot=prot,.kind=kind});
}
bool nas_anon_record(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length) {
    if(addr>=NAS_ANON_CEILING || length>NAS_ANON_CEILING-addr)return false;
    return nas_vma_record(regions,addr,length,0,NAS_VMA_ANON_PRIVATE,0);
}
bool nas_anon_forget(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length) {
    if (!regions || !length || !aligned(addr) || !aligned(length) ||
        addr>UINT64_MAX-length) return false;
    uint64_t end=addr+length;
    int split=-1;
    for (unsigned i=0;i<regions->capacity;++i) {
        const struct nas_anon_region *r=&regions->slots[i];
        if (r->end && addr>r->start && end<r->end) {
            if (split>=0) return false;
            split=(int)i;
        }
    }
    int spare=split>=0 ? empty_slot(regions) : -1;
    if (split>=0 && spare<0) {
        if(!grow_regions(regions))return false;
        spare=empty_slot(regions);
    }
    for (unsigned i=0;i<regions->capacity;++i) {
        struct nas_anon_region *r=&regions->slots[i];
        if (!r->end || addr>=r->end || end<=r->start) continue;
        if (addr<=r->start && end>=r->end) *r=(struct nas_anon_region){0};
        else if (addr<=r->start) {
            if((r->kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT || r->kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT))
                r->file_offset+=end-r->start;
            r->start=end;
        } else if (end>=r->end) r->end=addr;
        else {
            struct nas_anon_region right=*r;
            right.start=end;
            if((right.kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT || right.kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT))
                right.file_offset+=end-r->start;
            regions->slots[spare]=right;
            r->end=addr;
        }
    }
    return true;
}

/* Validate an entire interval without scanning one page at a time. */
bool nas_anon_covered(const struct nas_anon_regions *regions,
                      uint64_t addr,uint64_t length) {
    if (!regions || !valid_interval(addr,length)) return false;
    uint64_t pos=addr, end=addr+length;
    while (pos<end) {
        uint64_t next=pos;
        for (unsigned i=0;i<regions->capacity;i++) {
            const struct nas_anon_region *r=&regions->slots[i];
            if (r->end && r->start<=pos && r->end>next) next=r->end;
        }
        if (next==pos) return false;
        pos=next;
    }
    return true;
}
bool nas_anon_intersects(const struct nas_anon_regions *regions,
                         uint64_t addr,uint64_t length) {
    if (!regions || !length || addr>UINT64_MAX-length) return false;
    uint64_t end=addr+length;
    for (unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *r=&regions->slots[i];
        if (r->end && addr<r->end && r->start<end) return true;
    }
    return false;
}
bool nas_anon_overlay(struct nas_anon_regions *regions,
                      uint64_t addr,uint64_t length) {
    if (!regions || !valid_interval(addr,length) ||
        addr>=NAS_ANON_CEILING || length>NAS_ANON_CEILING-addr)
        return false;
    if (nas_anon_covered(regions,addr,length)) return true;
    struct nas_anon_regions next={0};
    if (!nas_vma_clone(&next,regions))return false;
    if (!nas_anon_forget(&next,addr,length) ||
        !nas_anon_record(&next,addr,length)) {
        nas_anon_reset(&next);
        return false;
    }
    nas_vma_commit(regions,&next);
    return true;
}

/* Replacement is staged in a separately owned allocation so a grow/split
 * failure cannot mutate the live MM's policy or free its storage. */
bool nas_vma_replace(struct nas_anon_regions *regions,uint64_t addr,uint64_t length,
                     unsigned prot,unsigned kind,uint64_t file_offset) {
    if(!regions || !valid_interval(addr,length) || (prot&~7U) ||
       kind>NAS_VMA_SIGNAL_TRAMPOLINE)return false;
    struct nas_anon_regions planned={0};
    if(!nas_vma_clone(&planned,regions))return false;
    if(!nas_anon_forget(&planned,addr,length) ||
       !nas_vma_record(&planned,addr,length,prot,kind,file_offset)) {
        nas_anon_reset(&planned);
        return false;
    }
    nas_vma_commit(regions,&planned);
    return true;
}
/* Rebuild the metadata in a private staging set. Permission changes may
 * split existing VMAs, but must never change snapshot file offsets. */
bool nas_vma_protect(struct nas_anon_regions *regions,uint64_t addr,uint64_t length,
                     unsigned prot) {
    if(!regions || (prot&~7U) || !nas_anon_covered(regions,addr,length))
        return false;
    const uint64_t end=addr+length;
    struct nas_anon_regions planned={0};
    for(unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *r=&regions->slots[i];
        if(!r->end)continue;
        if(r->end<=addr || r->start>=end) {
            if(!insert_area(&planned,*r))goto failed;
            continue;
        }
        uint64_t middle_start=r->start>addr?r->start:addr;
        uint64_t middle_end=r->end<end?r->end:end;
        if(r->start<middle_start) {
            struct nas_anon_region left=*r;
            left.end=middle_start;
            if(!insert_area(&planned,left))goto failed;
        }
        struct nas_anon_region middle=*r;
        middle.start=middle_start;
        middle.end=middle_end;
        middle.prot=prot;
        if((middle.kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT || middle.kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT))
            middle.file_offset+=middle_start-r->start;
        if(!insert_area(&planned,middle))goto failed;
        if(middle_end<r->end) {
            struct nas_anon_region right=*r;
            right.start=middle_end;
            if((right.kind==NAS_VMA_FILE_PRIVATE_SNAPSHOT || right.kind==NAS_VMA_ELF_PRIVATE_SNAPSHOT))
                right.file_offset+=middle_end-r->start;
            if(!insert_area(&planned,right))goto failed;
        }
    }
    nas_vma_commit(regions,&planned);
    return true;
failed:
    nas_anon_reset(&planned);
    return false;
}
const struct nas_anon_region *nas_vma_find(const struct nas_anon_regions *regions,
                                           uint64_t va) {
    if(!regions)return 0;
    for(unsigned i=0;i<regions->capacity;i++) {
        const struct nas_anon_region *r=&regions->slots[i];
        if(r->end && r->start<=va && va<r->end)return r;
    }
    return 0;
}
bool nas_vma_walk(const struct nas_anon_regions *regions,nas_vma_visit cb,void *ctx) {
    if(!regions || !cb)return false;
    uint64_t next=NAS_ANON_FIXED_FLOOR;
    for(unsigned count=0;count<regions->capacity;count++) {
        const struct nas_anon_region *best=0;
        for(unsigned i=0;i<regions->capacity;i++) {
            const struct nas_anon_region *r=&regions->slots[i];
            if(!r->end || r->start<next)continue;
            if(!best || r->start<best->start)best=r;
        }
        if(!best)return true;
        if(!cb(best,ctx))return false;
        next=best->end;
    }
    return true;
}
