#include "../kernel/mm/heap.h"
#include <assert.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>

static alignas(max_align_t) unsigned char extra[4U * 1024U * 1024U];
static bool supplied;
static bool grow(size_t bytes, void *context) {
    (void)context;
    if (supplied || bytes > sizeof(extra)) return false;
    supplied = nas_heap_add_arena(extra, sizeof(extra));
    return supplied;
}
int main(void) {
    assert(!nas_heap_alloc(0));
    assert(!nas_heap_calloc(SIZE_MAX, 2));
    assert(!nas_heap_alloc(SIZE_MAX));
    assert(nas_heap_free(NULL));
    unsigned char *p = nas_heap_calloc(64, 4);
    assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
    for (unsigned i = 0; i < 256; ++i) assert(p[i] == 0);
    memset(p, 0x5a, 256);
    assert(!nas_heap_free(p + _Alignof(max_align_t)));
    assert(!nas_heap_free(&supplied));
    assert(!nas_heap_realloc(p, SIZE_MAX));
    p = nas_heap_realloc(p, 4096);
    assert(p && (uintptr_t)p % _Alignof(max_align_t) == 0);
    for (unsigned i = 0; i < 256; ++i) assert(p[i] == 0x5a);
    p = nas_heap_realloc(p, 128);
    assert(p && p[127] == 0x5a);
    assert(nas_heap_free(p));
    assert(!nas_heap_free(p));
    nas_heap_set_grow_callback(grow, NULL);
    void *big = nas_heap_alloc(1536U * 1024U);
    assert(big && supplied);
    assert(!nas_heap_add_arena(extra, sizeof(extra)));
    assert(nas_heap_free(big));
    /* Exercise many small, differently aligned blocks and reuse after free. */
    void *blocks[256];
    for (unsigned i = 0; i < 256; ++i) {
        blocks[i] = nas_heap_alloc(i + 1);
        assert(blocks[i] && (uintptr_t)blocks[i] % _Alignof(max_align_t) == 0);
    }
    for (unsigned i = 0; i < 256; i += 2) assert(nas_heap_free(blocks[i]));
    for (unsigned i = 1; i < 256; i += 2) assert(nas_heap_free(blocks[i]));
    big = nas_heap_alloc(3U * 1024U * 1024U);
    assert(big && nas_heap_free(big));
    return 0;
}
