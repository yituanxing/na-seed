#include "string.h"
#include <stdint.h>

/* These baseline implementations intentionally do not depend on any libc
 * function or platform assembly. Volatile byte access for copy/fill prevents
 * an optimizing compiler from recognizing a loop and emitting a recursive
 * call to memcpy/memmove/memset while building those symbols themselves.
 * The usercopy layer STILL owns user PTE permissions and range validation.
 * Optimized ISA providers can replace these only after equivalent tests. */
void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    if ((uintptr_t)dest > (uintptr_t)src &&
        (uintptr_t)dest - (uintptr_t)src < n) {
        while (n) { --n; d[n] = s[n]; }
    } else {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t n) {
    volatile unsigned char *d = (volatile unsigned char *)dest;
    const unsigned char b = (unsigned char)value;
    for (size_t i = 0; i < n; i++) d[i] = b;
    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t n) {
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

int strcmp(const char *lhs, const char *rhs) {
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    while (*a && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

int strncmp(const char *lhs, const char *rhs, size_t n) {
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

char *strchr(const char *s, int ch) {
    const unsigned char c = (unsigned char)ch;
    for (;;) {
        if ((unsigned char)*s == c) return (char *)s;
        if (!*s) return NULL;
        ++s;
    }
}

char *strrchr(const char *s, int ch) {
    const unsigned char c = (unsigned char)ch;
    const char *found = NULL;
    for (;;) {
        if ((unsigned char)*s == c) found = s;
        if (!*s) return (char *)found;
        ++s;
    }
}
