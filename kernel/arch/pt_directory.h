#ifndef NASEED_PT_DIRECTORY_H
#define NASEED_PT_DIRECTORY_H
#include <stdbool.h>
#include <stdint.h>
#include "../mm/frame.h"

/* Shared 4K/512-entry directory mechanics for RV64 and AArch64.
 * Descriptor encoding, leaf validity, VA limits and TLB policy stay with
 * the ISA. The caller validates root and levels (3..5) before entry.
 * Only directory frames belong to this layer, never mapped data frames. */
struct nas_pt_dir_ops {
    bool (*valid_pa)(uint64_t);
    bool (*is_table)(uint64_t);
    uint64_t (*decode)(uint64_t);
    uint64_t (*encode)(uint64_t);
};
struct nas_pt_dir_path {
    uint64_t *parent[4];
    uint64_t frame[4];
    unsigned created;
};
static inline uint64_t *nas_pt_table(uint64_t pa) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset()+pa);
}
static inline unsigned nas_pt_index(uint64_t va,unsigned level) {
    return (unsigned)((va>>(12+9*level))&511ULL);
}
static inline void nas_pt_dir_rollback(struct nas_pt_dir_path *path) {
    /* Release newly allocated directories deepest first. */
    for(unsigned i=0;i<4;i++)
        if(path->created&(1U<<i)) {
            *path->parent[i]=0;
            (void)nas_frame_put(path->frame[i]);
        }
    path->created=0;
}
static inline uint64_t *nas_pt_dir_leaf(const struct nas_pt_dir_ops *ops,
    uint64_t root,unsigned levels,uint64_t va,
    struct nas_pt_dir_path *path,bool create) {
    uint64_t *tab=nas_pt_table(root);
    for(unsigned level=levels-1;level>0;level--) {
        unsigned i=level-1;
        uint64_t *slot=&tab[nas_pt_index(va,level)];
        uint64_t pa;
        if(!*slot && create) {
            pa=nas_frame_alloc();
            if(!ops->valid_pa(pa)) {
                if(pa)(void)nas_frame_put(pa);
                goto fail;
            }
            *slot=ops->encode(pa);
            path->created|=1U<<i;
        } else {
            if(!ops->is_table(*slot))goto fail;
            pa=ops->decode(*slot);
        }
        if(path) {
            path->parent[i]=slot;
            path->frame[i]=pa;
        }
        tab=nas_pt_table(pa);
    }
    return &tab[nas_pt_index(va,0)];
fail:
    if(create)nas_pt_dir_rollback(path);
    return 0;
}
static inline bool nas_pt_dir_reclaim(unsigned levels,
                                      const struct nas_pt_dir_path *path) {
    for(unsigned i=0;i<levels-1;i++) {
        const uint64_t *tab=nas_pt_table(path->frame[i]);
        for(unsigned n=0;n<512;n++)
            if(tab[n])return true;
        if(!nas_frame_put(path->frame[i]))return false;
        *path->parent[i]=0;
    }
    return true;
}
static inline void nas_pt_dir_destroy(const struct nas_pt_dir_ops *ops,
                                      uint64_t pa,unsigned level) {
    if(!level)return;
    const uint64_t *tab=nas_pt_table(pa);
    for(unsigned i=0;i<512;i++)
        if(ops->is_table(tab[i])) {
            uint64_t child=ops->decode(tab[i]);
            nas_pt_dir_destroy(ops,child,level-1);
            (void)nas_frame_put(child);
        }
}
#endif
