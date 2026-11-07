#ifndef NASEED_LWIP_FREESTANDING_STDIO_H
#define NASEED_LWIP_FREESTANDING_STDIO_H
#include <stddef.h>
/* Upstream mem.c includes stdio even when MEM_OVERFLOW_CHECK=0; its guarded
 * debug-only snprintf calls must never pull glibc into the kernel link. */
int snprintf(char *restrict dst,size_t n,const char *restrict fmt,...);
#endif
