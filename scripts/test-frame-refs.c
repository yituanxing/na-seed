/* Test the real physical-page allocator with a synthetic Limine RAM map. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdalign.h>
#include <string.h>
#include <limine.h>
#include "../kernel/mm/frame.h"
#define NPAGE 96U
#define PAGE 4096U
static alignas(PAGE) uint8_t ram[NPAGE * PAGE];
int main(void) {
    struct limine_memmap_entry entry={0};
    entry.base=PAGE;
    entry.length=sizeof(ram);
    entry.type=LIMINE_MEMMAP_USABLE;
    struct limine_memmap_entry *entries[]={&entry};
    struct limine_memmap_response map={0};
    map.entry_count=1;
    map.entries=entries;
    assert(nas_frames_init(&map,(uint64_t)(uintptr_t)ram-PAGE));
    const uint64_t baseline=nas_free_frames();
    assert(baseline<NPAGE && baseline>2);
    assert(nas_frame_refs(PAGE)==0 && !nas_frame_get(PAGE));
    uint64_t pa=nas_frame_alloc();
    assert(pa && pa!=PAGE && nas_frame_refs(pa)==1);
    assert(nas_free_frames()==baseline-1);
    assert(nas_frame_get(pa) && nas_frame_refs(pa)==2);
    assert(nas_frame_get(pa) && nas_frame_refs(pa)==3);
    assert(nas_frame_put(pa) && nas_frame_refs(pa)==2);
    assert(nas_frame_put(pa) && nas_frame_refs(pa)==1);
    assert(nas_free_frames()==baseline-1);
    assert(nas_frame_put(pa) && nas_frame_refs(pa)==0);
    assert(nas_free_frames()==baseline);
    assert(!nas_frame_put(pa) && !nas_frame_get(pa));
    assert(!nas_frame_alloc_contiguous(0));
    assert(!nas_frame_alloc_contiguous(baseline+1));
    assert(nas_free_frames()==baseline);
    uint64_t run=nas_frame_alloc_contiguous(8);
    assert(run && nas_free_frames()==baseline-8);
    for(unsigned i=0;i<8;i++) {
        assert(nas_frame_refs(run+i*PAGE)==1);
        uint8_t *page=(uint8_t *)(uintptr_t)(nas_hhdm_offset()+run+i*PAGE);
        for(unsigned j=0;j<PAGE;j++)assert(page[j]==0);
        memset(page,0xa5,PAGE);
    }
    for(unsigned i=0;i<8;i++)assert(nas_frame_put(run+i*PAGE));
    assert(nas_free_frames()==baseline);
    uint64_t run_again=nas_frame_alloc_contiguous(8);
    assert(run_again==run);
    for(unsigned i=0;i<8;i++) {
        uint8_t *page=(uint8_t *)(uintptr_t)(nas_hhdm_offset()+run+i*PAGE);
        for(unsigned j=0;j<PAGE;j++)assert(page[j]==0);
        assert(nas_frame_put(run+i*PAGE));
    }
    uint64_t allocated[NPAGE]={0};
    unsigned count=0;
    while(count<NPAGE && (allocated[count]=nas_frame_alloc())) {
        assert(nas_frame_refs(allocated[count])==1);
        count++;
    }
    assert(count==baseline && nas_free_frames()==0);
    assert(!nas_frame_alloc());
    assert(!nas_frame_alloc_contiguous(2));
    for(unsigned i=0;i<count;i+=2)assert(nas_frame_put(allocated[i]));
    uint64_t free_before=nas_free_frames();
    /* Alternating released/retained frames leave no two-frame free run. */
    assert(!nas_frame_alloc_contiguous(2));
    assert(nas_free_frames()==free_before);
    for(unsigned i=1;i<count;i+=2) {
        assert(nas_frame_refs(allocated[i])==1);
        assert(nas_frame_put(allocated[i]));
    }
    assert(nas_free_frames()==baseline);
    puts("[M83 PAGE REFS PASS] get/put, contiguous zeroing, atomic failure, pinned metadata, OOM");
    return 0;
}
