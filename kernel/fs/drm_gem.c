/* Linux GEM dumb buffer backing, independent of VirtIO GPU command queues.
 * This is system RAM suitable for MAP_SHARED software rendering, not yet a
 * KMS framebuffer or a GPU-submitted scanout surface. No shadow copies.
 *
 * One handle reference belongs to the opening shared OpenFile. Every VMA
 * takes its own independent owner pin through nas_vm_device. Pages are freed
 * only when BOTH handles and VMAs have vanished; user PTEs also own PMM refs.
 * mmap offsets are never reused during a boot, so stale cookies cannot alias
 * a new object after the originating handle was destroyed. */
#include "drm_gem.h"
#include "../mm/device_backing.h"
#include "../mm/frame.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../errno.h"
#include "../fd.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define GEM_MAX_OBJECTS 32U
#define GEM_MAX_BYTES (32ULL*1024ULL*1024ULL)
#define GEM_PAGE 4096ULL
/* Nonzero Linux DRM fake mmap offset; this is not a physical address. */
#define GEM_FIRST_OFFSET UINT64_C(0x10000000)
struct gem_object {
    struct nas_vm_device vm;
    uint64_t *frames;
    uint64_t size,rounded_bytes,mmap_offset;
    uint32_t handle,pitch;
    unsigned pages,owner_slot,refs;
    bool handle_live;
};
static struct gem_object objects[GEM_MAX_OBJECTS];
static uint32_t next_handle=1U;
static uint64_t next_offset=GEM_FIRST_OFFSET;

static struct gem_object *find_handle(unsigned slot,uint32_t handle) {
    if(!handle)return NULL;
    for(unsigned i=0;i<GEM_MAX_OBJECTS;i++) {
        struct gem_object *o=&objects[i];
        if(o->handle_live && o->owner_slot==slot && o->handle==handle)
            return o;
    }
    return NULL;
}
static void object_put(struct gem_object *o) {
    if(!o || !o->refs)__builtin_trap();
    if(--o->refs)return;
    /* A zero owner count means no VMA references remain. Their PTE refs
     * have already been dropped (or must be dropped by ordinary MM unmap). */
    for(unsigned i=0;i<o->pages;i++)
        if(o->frames[i] && !nas_frame_put(o->frames[i]))
            __builtin_trap();
    if(o->frames && !nas_heap_free(o->frames))__builtin_trap();
    *o=(struct gem_object){0};
}
static bool vm_get(void *context) {
    struct gem_object *o=context;
    if(!o || !o->frames || !o->refs || o->refs==UINT32_MAX)return false;
    ++o->refs;
    return true;
}
static void vm_put(void *context) {object_put(context);}
static int vm_page(void *context,uint64_t offset,uint64_t *out) {
    struct gem_object *o=context;
    if(!out)return -EINVAL;
    *out=0;
    if(!o || !o->refs || !o->frames ||
       (offset&(GEM_PAGE-1U)) || offset<o->mmap_offset ||
       offset-o->mmap_offset>=o->rounded_bytes)return -EINVAL;
    unsigned index=(unsigned)((offset-o->mmap_offset)/GEM_PAGE);
    if(index>=o->pages || !o->frames[index])return -EIO;
    *out=o->frames[index];
    return 0;
}
int nas_drm_gem_create(unsigned slot,uint32_t width,uint32_t height,
                       uint32_t bpp,uint32_t flags,
                       struct nas_drm_gem_created *out) {
    if(!out || slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    *out=(struct nas_drm_gem_created){0};
    if(!width || !height || !bpp || bpp>32U || flags)return -EINVAL;
    uint64_t bits=(uint64_t)width*bpp;
    uint64_t pitch=((bits+7U)/8U+3U)&~UINT64_C(3);
    if(!pitch || pitch>UINT32_MAX || height>UINT64_MAX/pitch)
        return -EINVAL;
    uint64_t size=pitch*height;
    if(!size || size>GEM_MAX_BYTES)return -ENOMEM;
    uint64_t rounded=(size+GEM_PAGE-1U)&~(GEM_PAGE-1U);
    unsigned pages=(unsigned)(rounded/GEM_PAGE);
    if(next_handle==UINT32_MAX ||
       next_offset>INT64_MAX-rounded-GEM_PAGE)return -ENOSPC;
    struct gem_object *o=NULL;
    for(unsigned i=0;i<GEM_MAX_OBJECTS;i++)
        if(!objects[i].refs) {o=&objects[i];break;}
    if(!o)return -ENOSPC;
    uint64_t *frames=nas_heap_calloc(pages,sizeof(*frames));
    if(!frames)return -ENOMEM;
    unsigned allocated=0;
    for(;allocated<pages;allocated++) {
        frames[allocated]=nas_frame_alloc();
        if(!frames[allocated])break;
    }
    if(allocated!=pages) {
        for(unsigned i=0;i<allocated;i++)
            if(!nas_frame_put(frames[i]))__builtin_trap();
        if(!nas_heap_free(frames))__builtin_trap();
        return -ENOMEM;
    }
    *o=(struct gem_object){
        .frames=frames,.size=size,.rounded_bytes=rounded,
        .mmap_offset=next_offset,.handle=next_handle++,
        .pitch=(uint32_t)pitch,.pages=pages,.owner_slot=slot,
        .refs=1,.handle_live=true
    };
    o->vm=(struct nas_vm_device){
        .context=o,.extent_bytes=o->mmap_offset+rounded,
        .get=vm_get,.put=vm_put,.page=vm_page
    };
    next_offset+=rounded+GEM_PAGE;
    *out=(struct nas_drm_gem_created){
        .handle=o->handle,.pitch=o->pitch,.size=size
    };
    return 0;
}
int nas_drm_gem_map_offset(unsigned slot,uint32_t handle,uint64_t *out) {
    if(!out || slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    *out=0;
    struct gem_object *o=find_handle(slot,handle);
    if(!o)return -EINVAL;
    *out=o->mmap_offset;
    return 0;
}
int nas_drm_gem_destroy(unsigned slot,uint32_t handle) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    struct gem_object *o=find_handle(slot,handle);
    if(!o)return -EINVAL;
    o->handle_live=false;
    object_put(o);
    return 0;
}
int nas_drm_gem_mmap_pin(unsigned slot,uint64_t offset,
                         struct nas_vm_device **out) {
    if(!out || slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    *out=NULL;
    for(unsigned i=0;i<GEM_MAX_OBJECTS;i++) {
        struct gem_object *o=&objects[i];
        if(!o->handle_live || o->owner_slot!=slot ||
           offset<o->mmap_offset ||
           offset-o->mmap_offset>=o->rounded_bytes)continue;
        if(!o->vm.get(o))return -EBUSY;
        *out=&o->vm;
        return 0;
    }
    return -EINVAL;
}
int nas_drm_gem_pin_fb(unsigned slot,uint32_t handle,
                       struct nas_vm_device **out,uint64_t *bytes,
                       uint32_t *pitch) {
    if(!out || !bytes || !pitch)return -EINVAL;
    *out=NULL;*bytes=0;*pitch=0;
    struct gem_object *o=find_handle(slot,handle);
    if(!o)return -ENOENT;
    if(!vm_get(o))return -EBUSY;
    *out=&o->vm;
    *bytes=o->size;
    *pitch=o->pitch;
    return 0;
}
int nas_drm_gem_copy_from(struct nas_vm_device *owner,uint64_t offset,
                           uint8_t *dst,uint64_t bytes) {
    if(!owner || !dst)return -EINVAL;
    struct gem_object *o=owner->context;
    if(!o || &o->vm!=owner || !o->refs || !o->frames ||
       offset>o->size || bytes>o->size-offset)return -EINVAL;
    uint64_t done=0;
    while(done<bytes) {
        uint64_t at=offset+done;
        unsigned page=(unsigned)(at/GEM_PAGE);
        uint64_t within=at%GEM_PAGE;
        uint64_t chunk=GEM_PAGE-within;
        if(chunk>bytes-done)chunk=bytes-done;
        if(page>=o->pages || !o->frames[page])return -EIO;
        memcpy(dst+done,(const void *)(uintptr_t)
               (nas_hhdm_offset()+o->frames[page]+within),(size_t)chunk);
        done+=chunk;
    }
    return 0;
}
void nas_drm_gem_file_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    for(unsigned i=0;i<GEM_MAX_OBJECTS;i++) {
        struct gem_object *o=&objects[i];
        if(o->handle_live && o->owner_slot==slot) {
            o->handle_live=false;
            object_put(o);
        }
    }
}
