#include "heap.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
/* Backend-agnostic arena allocator: later VM supplies extra arena mappings
 * without changing callers or the ext4 port. The initial buffer is not
 * allocated from physical frames and never aliases a user address. */
#define NAS_HEAP_BOOT_BYTES (1024U * 1024U)
#define NAS_HEAP_ARENAS 16U
#define NAS_HEAP_GROW_START (2U * 1024U * 1024U)
#define NAS_HEAP_GROW_LIMIT (16U * 1024U * 1024U)
#define NAS_HEAP_ALIGN ((size_t)_Alignof(max_align_t))
struct heap_block {
    alignas(max_align_t) struct heap_block *next;
    size_t size;
    bool available;
};
struct heap_arena {
    uintptr_t start;
    size_t bytes;
    struct heap_block *first;
};
static alignas(max_align_t) unsigned char boot_area[NAS_HEAP_BOOT_BYTES];
static struct heap_arena arenas[NAS_HEAP_ARENAS];
static unsigned arena_count;
static bool ready;
static bool growing;
static nas_heap_grow_callback grow_callback;
static void *grow_context;
static size_t next_grow_bytes = NAS_HEAP_GROW_START;
static size_t align_up(size_t n) {
    return (n + NAS_HEAP_ALIGN - 1) & ~(NAS_HEAP_ALIGN - 1);
}
static bool add(void *storage,size_t length) {
    if(!storage || arena_count>=NAS_HEAP_ARENAS)return false;
    uintptr_t base=(uintptr_t)storage;
    if(base>UINTPTR_MAX-(NAS_HEAP_ALIGN-1))return false;
    uintptr_t aligned=(base+NAS_HEAP_ALIGN-1)&~(uintptr_t)(NAS_HEAP_ALIGN-1);
    if(aligned<base || aligned-base>=length)return false;
    length-=aligned-base;
    length&=~(NAS_HEAP_ALIGN-1);
    if(length<=sizeof(struct heap_block)+NAS_HEAP_ALIGN ||
       aligned>UINTPTR_MAX-length)return false;
    for(unsigned i=0;i<arena_count;i++) {
        uintptr_t first=arenas[i].start,last=first+arenas[i].bytes;
        if(aligned<last && first<aligned+length)return false;
    }
    struct heap_block *block=(struct heap_block *)aligned;
    *block=(struct heap_block){.next=NULL,
        .size=length-sizeof(struct heap_block),.available=true};
    arenas[arena_count++]=(struct heap_arena){
        .start=aligned,.bytes=length,.first=block};
    return true;
}
static void ensure_ready(void) {
    if(ready)return;
    ready=true;
    (void)add(boot_area,sizeof(boot_area));
}
bool nas_heap_add_arena(void *memory,size_t length) {
    ensure_ready();
    return add(memory,length);
}
void nas_heap_set_grow_callback(nas_heap_grow_callback callback, void *context) {
    grow_callback=callback;
    grow_context=context;
}
static void split(struct heap_block *block,size_t needed) {
    if(block->size<needed+sizeof(struct heap_block)+NAS_HEAP_ALIGN)return;
    uintptr_t where=(uintptr_t)(block+1)+needed;
    struct heap_block *next=(struct heap_block *)where;
    *next=(struct heap_block){.next=block->next,
        .size=block->size-needed-sizeof(struct heap_block),
        .available=true};
    block->size=needed;
    block->next=next;
}
static void coalesce(struct heap_block *block) {
    while(block->next && block->next->available) {
        struct heap_block *next=block->next;
        block->size+=sizeof(struct heap_block)+next->size;
        block->next=next->next;
    }
}
static void *alloc_existing(size_t n) {
    for(unsigned i=0;i<arena_count;i++) {
        for(struct heap_block *b=arenas[i].first;b;b=b->next) {
            if(!b->available || b->size<n)continue;
            split(b,n);
            b->available=false;
            return b+1;
        }
    }
    return NULL;
}
void *nas_heap_alloc(size_t length) {
    if(!length || length>SIZE_MAX-(NAS_HEAP_ALIGN-1))return NULL;
    ensure_ready();
    size_t n=align_up(length);
    void *result=alloc_existing(n);
    if(result || !grow_callback || growing || arena_count>=NAS_HEAP_ARENAS)
        return result;
    if(n>SIZE_MAX-sizeof(struct heap_block)-(NAS_HEAP_ALIGN-1))return NULL;
    const size_t minimum=n+sizeof(struct heap_block)+NAS_HEAP_ALIGN-1;
    size_t requested=minimum>next_grow_bytes?minimum:next_grow_bytes;
    /* A failed large contiguous reservation can still succeed at the exact
     * required size: do not turn physical fragmentation into false ENOMEM. */
    growing=true;
    bool added=grow_callback(requested,grow_context);
    if(!added && requested>minimum)
        added=grow_callback(minimum,grow_context);
    growing=false;
    if(!added)return NULL;
    if(next_grow_bytes<NAS_HEAP_GROW_LIMIT) {
        size_t doubled=next_grow_bytes*2;
        next_grow_bytes=doubled>NAS_HEAP_GROW_LIMIT?
                         NAS_HEAP_GROW_LIMIT:doubled;
    }
    return alloc_existing(n);
}
static struct heap_block *find(void *ptr,struct heap_arena **owner,
                                struct heap_block **previous) {
    if(!ptr)return NULL;
    uintptr_t target=(uintptr_t)ptr;
    for(unsigned i=0;i<arena_count;i++) {
        struct heap_arena *a=&arenas[i];
        if(target<a->start+sizeof(struct heap_block) ||
           target>=a->start+a->bytes)continue;
        struct heap_block *last=NULL;
        for(struct heap_block *b=a->first;b;b=b->next) {
            if((uintptr_t)(b+1)==target) {
                if(owner)*owner=a;
                if(previous)*previous=last;
                return b;
            }
            last=b;
        }
    }
    return NULL;
}
bool nas_heap_free(void *ptr) {
    if(!ptr)return true;
    ensure_ready();
    struct heap_block *prev=NULL,*b=find(ptr,NULL,&prev);
    if(!b || b->available)return false;
    b->available=true;
    coalesce(b);
    if(prev && prev->available)coalesce(prev);
    return true;
}
void *nas_heap_calloc(size_t count,size_t size) {
    if(size && count>SIZE_MAX/size)return NULL;
    size_t n=count*size;
    void *ptr=nas_heap_alloc(n);
    if(ptr)memset(ptr,0,n);
    return ptr;
}
void *nas_heap_realloc(void *ptr,size_t length) {
    if(!ptr)return nas_heap_alloc(length);
    if(!length) { (void)nas_heap_free(ptr);return NULL; }
    if(length>SIZE_MAX-(NAS_HEAP_ALIGN-1))return NULL;
    ensure_ready();
    struct heap_block *b=find(ptr,NULL,NULL);
    if(!b || b->available)return NULL;
    size_t n=align_up(length);
    if(b->size>=n) {
        split(b,n);
        if(b->next && b->next->available)coalesce(b->next);
        return ptr;
    }
    /* Filesystem buffers often grow a few KiB at a time. Reserve geometric
     * slack once, avoiding a full-file copy on every ramfs_vector realloc. */
    size_t reserve=n;
    if(b->size<=SIZE_MAX/2 && b->size*2>reserve)reserve=b->size*2;
    if(b->next && b->next->available) {
        size_t adjacent=b->size+sizeof(struct heap_block)+b->next->size;
        if(adjacent>=n) {
            coalesce(b);
            split(b,adjacent>=reserve?reserve:n);
            return ptr;
        }
    }
    void *replacement=nas_heap_alloc(reserve);
    if(!replacement && reserve>n)replacement=nas_heap_alloc(n);
    if(!replacement)return NULL;
    memcpy(replacement,ptr,b->size);
    (void)nas_heap_free(ptr);
    return replacement;
}
