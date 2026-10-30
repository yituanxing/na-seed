#ifndef NASEED_MM_HEAP_H
#define NASEED_MM_HEAP_H
#include <stddef.h>
#include <stdbool.h>

/* Freestanding kernel allocation API, independent of ISA or filesystem.
 * Bootstrap arena is bounded; additional non-overlapping, addressable
 * arenas may be registered directly or via the physical-frame grow callback.
 * Only call from kernel process context, never an interrupt handler.
 */
bool nas_heap_add_arena(void *memory, size_t length);
/* Called only after the boot arena cannot satisfy an allocation. The callback
 * may register a non-overlapping arena with nas_heap_add_arena; it must not
 * recursively allocate from the heap. NULL disables on-demand growth. */
typedef bool (*nas_heap_grow_callback)(size_t minimum_bytes, void *context);
void nas_heap_set_grow_callback(nas_heap_grow_callback callback, void *context);
void *nas_heap_alloc(size_t length);
void *nas_heap_calloc(size_t count, size_t size);
void *nas_heap_realloc(void *ptr, size_t length);
bool nas_heap_free(void *ptr);
#endif
