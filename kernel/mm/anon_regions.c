#include "anon_regions.h"
#include <stddef.h>
#define PAGE 4096ULL
static bool aligned(uint64_t n) { return (n&(PAGE-1))==0; }
static bool valid_interval(uint64_t start,uint64_t length) {
    return length && aligned(start) && aligned(length) &&
           start>=NAS_ANON_FLOOR && start<NAS_ANON_CEILING &&
           length<=NAS_ANON_CEILING-start;
}
void nas_anon_reset(struct nas_anon_regions *regions) {
    if (!regions) return;
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i)
        regions->slots[i]=(struct nas_anon_region){0,0};
}
static int empty_slot(const struct nas_anon_regions *regions) {
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i)
        if (!regions->slots[i].end) return (int)i;
    return -1;
}
bool nas_anon_find_gap(const struct nas_anon_regions *regions,
                       uint64_t length,uint64_t *addr) {
    if (!regions || !addr || !length || !aligned(length) ||
        length>NAS_ANON_CEILING-NAS_ANON_FLOOR ||
        empty_slot(regions)<0) return false;
    uint64_t top=NAS_ANON_CEILING;
    for (unsigned round=0;round<=NAS_ANON_REGION_LIMIT;++round) {
        if (top<NAS_ANON_FLOOR+length) return false;
        uint64_t start=top-length, next_top=top;
        bool collided=false;
        for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i) {
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
bool nas_anon_record(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length) {
    if (!regions || !valid_interval(addr,length)) return false;
    int slot=empty_slot(regions);
    if (slot<0) return false;
    uint64_t end=addr+length;
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i) {
        const struct nas_anon_region *r=&regions->slots[i];
        if (r->end && addr<r->end && r->start<end) return false;
    }
    regions->slots[slot]=(struct nas_anon_region){addr,end};
    return true;
}
bool nas_anon_forget(struct nas_anon_regions *regions,
                     uint64_t addr,uint64_t length) {
    if (!regions || !length || !aligned(addr) || !aligned(length) ||
        addr>UINT64_MAX-length) return false;
    uint64_t end=addr+length;
    int split=-1;
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i) {
        const struct nas_anon_region *r=&regions->slots[i];
        if (r->end && addr>r->start && end<r->end) {
            if (split>=0) return false;
            split=(int)i;
        }
    }
    int spare=split>=0 ? empty_slot(regions) : -1;
    if (split>=0 && spare<0) return false;
    for (unsigned i=0;i<NAS_ANON_REGION_LIMIT;++i) {
        struct nas_anon_region *r=&regions->slots[i];
        if (!r->end || addr>=r->end || end<=r->start) continue;
        if (addr<=r->start && end>=r->end) *r=(struct nas_anon_region){0,0};
        else if (addr<=r->start) r->start=end;
        else if (end>=r->end) r->end=addr;
        else {
            regions->slots[spare]=(struct nas_anon_region){end,r->end};
            r->end=addr;
        }
    }
    return true;
}
