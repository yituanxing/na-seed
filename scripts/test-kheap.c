#include "../kernel/mm/heap.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static unsigned char extra[8192];
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
    puts("[KHEAP PASS] aligned allocation, coalesce, in-place growth, calloc, realloc, arenas, overflow, invalid free");
    return 0;
}
