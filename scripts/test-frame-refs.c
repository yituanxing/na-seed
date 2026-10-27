/* Test the real physical-page allocator with a synthetic Limine RAM map. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdalign.h>
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
    uint64_t allocated[NPAGE]={0};
    unsigned count=0;
    while(count<NPAGE && (allocated[count]=nas_frame_alloc())) {
        assert(nas_frame_refs(allocated[count])==1);
        count++;
    }
    assert(count==baseline && nas_free_frames()==0);
    assert(!nas_frame_alloc());
    for(unsigned i=0;i<count;i++)assert(nas_frame_put(allocated[i]));
    assert(nas_free_frames()==baseline);
    puts("[M83 PAGE REFS PASS] get/put, final free, pinned metadata, OOM");
    return 0;
}
