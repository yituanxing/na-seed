#ifndef NASEED_RAMFS_PORT_STDLIB_H
#define NASEED_RAMFS_PORT_STDLIB_H
#include <stddef.h>
#include "../../../kernel/mm/heap.h"
void nas_ramfs_free(void *);
#define malloc nas_heap_alloc
#define calloc nas_heap_calloc
#define realloc nas_heap_realloc
#define free nas_ramfs_free
#endif
