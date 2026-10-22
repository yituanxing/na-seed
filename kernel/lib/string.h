#ifndef NASEED_KERNEL_LIB_STRING_H
#define NASEED_KERNEL_LIB_STRING_H

#include <stddef.h>

/* Freestanding kernel-only C primitives. No user address validation, heap,
 * syscalls, global state, allocator or libc runtime dependency. */
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
int memcmp(const void *lhs, const void *rhs, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *lhs, const char *rhs);
int strncmp(const char *lhs, const char *rhs, size_t n);
char *strchr(const char *s, int ch);
char *strrchr(const char *s, int ch);

#endif
