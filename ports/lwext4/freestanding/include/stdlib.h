#ifndef NASEED_LWEXT4_PORT_STDLIB_H
#define NASEED_LWEXT4_PORT_STDLIB_H
#include <stddef.h>
/* Only libc interfaces used by upstream lwext4. Do not supply a fake libc. */
void qsort(void *base,size_t count,size_t element_size,
           int (*compare)(const void *,const void *));
#endif
