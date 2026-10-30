#ifndef NAS_UBOOT_MALLOC_H
#define NAS_UBOOT_MALLOC_H
#include "../../../kernel/mm/heap.h"
#define malloc(n) nas_heap_alloc(n)
#define calloc(n,s) nas_heap_calloc((n),(s))
#define free(p) ((void)nas_heap_free(p))
#endif
