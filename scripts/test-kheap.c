#include "../kernel/mm/heap.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdalign.h>
static unsigned char extra[8192];
static alignas(max_align_t) unsigned char expanded[4U*1024U*1024U];
static bool fail_growth;
static bool expanded_added;
static unsigned grow_calls;
static bool test_grow(size_t bytes,void *context) {
    assert(context==expanded);
    ++grow_calls;
    if(fail_growth || expanded_added || bytes>sizeof(expanded))return false;
    if(!nas_heap_add_arena(expanded,sizeof(expanded)))return false;
    expanded_added=true;
    return true;
}
int main(void) {
    assert(nas_heap_alloc(0)==NULL);
    assert(nas_heap_alloc(SIZE_MAX)==NULL);
    assert(nas_heap_calloc(SIZE_MAX,2)==NULL);
    assert(nas_heap_free(NULL));
    void *a=nas_heap_alloc(32),*b=nas_heap_alloc(64),*c=nas_heap_alloc(32);
    assert(a && b && c && a!=b && b!=c);
    assert((uintptr_t)a%_Alignof(max_align_t)==0);
    memset(a,0x5a,32);
    assert(nas_heap_free(b));
    assert(!nas_heap_free(b)); /* double free is rejected */
    assert(nas_heap_free(c)); /* coalesces adjacent free blocks */
    void *grown=nas_heap_realloc(a,256);
    assert(grown==a); /* reclaimed contiguous blocks */
    for(unsigned i=0;i<32;i++)assert(((unsigned char *)grown)[i]==0x5a);
    assert(nas_heap_realloc(grown,16)==grown);
    assert(nas_heap_free(grown));
    assert(!nas_heap_free((unsigned char *)extra+7));
    assert(nas_heap_add_arena(extra+1,sizeof(extra)-1));
    assert(!nas_heap_add_arena(extra+16,1000));
    void *p=nas_heap_calloc(128,4);
    assert(p);
    for(unsigned i=0;i<512;i++)assert(!((unsigned char *)p)[i]);
    memset(p,0x83,512);
    void *q=nas_heap_realloc(p,300000);
    assert(q);
    for(unsigned i=0;i<512;i++)assert(((unsigned char *)q)[i]==0x83);
    assert(nas_heap_free(q));
    void *many[500];
    for(unsigned i=0;i<500;i++){many[i]=nas_heap_alloc(128);assert(many[i]);}
    for(unsigned i=0;i<500;i+=2)assert(nas_heap_free(many[i]));
    for(unsigned i=1;i<500;i+=2)assert(nas_heap_free(many[i]));
    assert(nas_heap_alloc(SIZE_MAX-3)==NULL);
    nas_heap_set_grow_callback(test_grow,expanded);
    fail_growth=true;
    assert(!nas_heap_alloc(1280U*1024U));
    assert(grow_calls>0 && !expanded_added);
    fail_growth=false;
    unsigned before=grow_calls;
    unsigned char *large=nas_heap_alloc(1280U*1024U);
    assert(large && expanded_added && grow_calls>before);
    memset(large,0x42,1280U*1024U);
    /* Realloc reserves geometric slack, so small growth steps stay in place. */
    assert(nas_heap_realloc(large,1300U*1024U)==large);
    assert(nas_heap_realloc(large,1600U*1024U)==large);
    assert(nas_heap_realloc(large,2100U*1024U)==large);
    for(unsigned i=0;i<1280U*1024U;i++)assert(large[i]==0x42);
    assert(!nas_heap_realloc(large,5U*1024U*1024U));
    for(unsigned i=0;i<1280U*1024U;i++)assert(large[i]==0x42);
    assert(nas_heap_free(large));
    nas_heap_set_grow_callback(NULL,NULL);
    puts("[KHEAP PASS] aligned allocation, coalesce, geometric realloc, >1MiB growth, rollback, OOM, invalid free");
    return 0;
}
