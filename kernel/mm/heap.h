#ifndef NASEED_MM_HEAP_H
#define NASEED_MM_HEAP_H
#include <stddef.h>
#include <stdbool.h>

/* Freestanding kernel allocation API, independent of ISA or filesystem.
 * Bootstrap arena is bounded; additional non-overlapping, addressable
 * arenas can be added when VM starts providing dynamically mapped ranges.
 * Only call from kernel process context, never an interrupt handler.
 */
bool nas_heap_add_arena(void *memory, size_t length);
void *nas_heap_alloc(size_t length);
void *nas_heap_calloc(size_t count, size_t size);
void *nas_heap_realloc(void *ptr, size_t length);
bool nas_heap_free(void *ptr);
#endif
