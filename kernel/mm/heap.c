#include "heap.h"
#include "../lib/string.h"
#include "../../third_party/tlsf/tlsf.h"
#include <stdint.h>
#include <stdalign.h>

/* TLSF 3.1, upstream mattconte/tlsf deff9ab509341f264addbd3c8ada533678591905.
 * NaSeed owns arena mappings and public API validation, not allocation policy.
 * Only kernel process context may use this interface. */
#define NAS_HEAP_BOOT_BYTES (1024U * 1024U)
#define NAS_HEAP_ARENAS 16U
#define NAS_HEAP_GROW_START (2U * 1024U * 1024U)
#define NAS_HEAP_GROW_LIMIT (16U * 1024U * 1024U)
#define NAS_HEAP_ALIGN ((size_t)_Alignof(max_align_t))

struct heap_arena {
    uintptr_t start, end;
    pool_t pool;
};
static alignas(max_align_t) unsigned char boot_area[NAS_HEAP_BOOT_BYTES];
static struct heap_arena arenas[NAS_HEAP_ARENAS];
static unsigned arena_count;
static tlsf_t heap;
static bool growing;
static nas_heap_grow_callback grow_callback;
static void *grow_context;
static size_t next_grow_bytes = NAS_HEAP_GROW_START;

static bool ensure_ready(void) {
    if (heap) return true;
    size_t control = tlsf_size();
    if (control >= sizeof(boot_area)) return false;
    tlsf_t candidate = tlsf_create(boot_area);
    if (!candidate) return false;
    pool_t pool = tlsf_add_pool(candidate, boot_area + control,
                                sizeof(boot_area) - control);
    if (!pool) return false;
    arenas[arena_count++] = (struct heap_arena){
        (uintptr_t)boot_area, (uintptr_t)boot_area + sizeof(boot_area), pool};
    heap = candidate;
    return true;
}

bool nas_heap_add_arena(void *memory, size_t length) {
    if (!memory || !ensure_ready() || arena_count >= NAS_HEAP_ARENAS)
        return false;
    const size_t align = tlsf_align_size();
    uintptr_t base = (uintptr_t)memory;
    if (base > UINTPTR_MAX - length || base > UINTPTR_MAX - (align - 1))
        return false;
    uintptr_t start = (base + align - 1) & ~(uintptr_t)(align - 1);
    if (start - base >= length) return false;
    size_t bytes = (length - (start - base)) & ~(align - 1);
    if (bytes < tlsf_pool_overhead() + tlsf_block_size_min() ||
        bytes >= tlsf_block_size_max()) return false;
    uintptr_t end = start + bytes;
    for (unsigned i = 0; i < arena_count; ++i)
        if (base < arenas[i].end && arenas[i].start < base + length)
            return false;
    pool_t pool = tlsf_add_pool(heap, (void *)start, bytes);
    if (!pool) return false;
    arenas[arena_count++] = (struct heap_arena){base, end, pool};
    return true;
}

void nas_heap_set_grow_callback(nas_heap_grow_callback callback, void *context) {
    grow_callback = callback;
    grow_context = context;
}

/* TLSF rounds requests into size classes; allow for that rounding as well
 * as pool metadata and max_align_t padding when asking PMM for an arena. */
static bool valid_size(size_t length) {
    return length && length < tlsf_block_size_max() - 256U;
}

void *nas_heap_alloc(size_t length) {
    if (!valid_size(length) || !ensure_ready()) return NULL;
    void *ptr = tlsf_memalign(heap, NAS_HEAP_ALIGN, length);
    if (ptr || !grow_callback || growing || arena_count >= NAS_HEAP_ARENAS)
        return ptr;
    size_t minimum = length + length / 16U + 256U;
    if (minimum >= tlsf_block_size_max()) return NULL;
    size_t requested = minimum > next_grow_bytes ? minimum : next_grow_bytes;
    growing = true;
    bool added = grow_callback(requested, grow_context);
    if (!added && requested > minimum)
        added = grow_callback(minimum, grow_context);
    growing = false;
    if (!added) return NULL;
    if (next_grow_bytes < NAS_HEAP_GROW_LIMIT)
        next_grow_bytes *= 2U;
    return tlsf_memalign(heap, NAS_HEAP_ALIGN, length);
}

/* The public API rejects foreign, interior and already freed pointers.
 * Use upstream pool traversal rather than maintaining a second block list. */
struct heap_lookup { const void *ptr; size_t size; };
static void lookup_used(void *ptr, size_t size, int used, void *context) {
    struct heap_lookup *lookup = context;
    if (used && ptr == lookup->ptr) lookup->size = size;
}
static size_t allocated_size(const void *ptr) {
    if (!ptr || !heap || (uintptr_t)ptr % NAS_HEAP_ALIGN) return 0;
    uintptr_t address = (uintptr_t)ptr;
    for (unsigned i = 0; i < arena_count; ++i) {
        if (address < (uintptr_t)arenas[i].pool || address >= arenas[i].end)
            continue;
        struct heap_lookup lookup = {ptr, 0};
        tlsf_walk_pool(arenas[i].pool, lookup_used, &lookup);
        return lookup.size;
    }
    return 0;
}

bool nas_heap_free(void *ptr) {
    if (!ptr) return true;
    if (!allocated_size(ptr)) return false;
    tlsf_free(heap, ptr);
    return true;
}

void *nas_heap_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    size_t bytes = count * size;
    void *ptr = nas_heap_alloc(bytes);
    if (ptr) memset(ptr, 0, bytes);
    return ptr;
}

void *nas_heap_realloc(void *ptr, size_t length) {
    if (!ptr) return nas_heap_alloc(length);
    if (!length) { (void)nas_heap_free(ptr); return NULL; }
    if (!valid_size(length)) return NULL;
    size_t old = allocated_size(ptr);
    if (!old) return NULL;
    /* Shrinking TLSF realloc cannot move the pointer. Growing via memalign
     * retains the stronger max_align_t guarantee even if TLSF would move it. */
    if (length <= old) return tlsf_realloc(heap, ptr, length);
    size_t reserve = length;
    if (old <= SIZE_MAX / 2U && old * 2U > reserve) reserve = old * 2U;
    void *replacement = nas_heap_alloc(reserve);
    if (!replacement && reserve > length) replacement = nas_heap_alloc(length);
    if (!replacement) return NULL;
    memcpy(replacement, ptr, old);
    tlsf_free(heap, ptr);
    return replacement;
}
