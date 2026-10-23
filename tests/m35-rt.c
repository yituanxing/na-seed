/* Freestanding C runtime slice, compiled as its own object by TinyCC
 * inside the real NaSeed guest. The upstream AES library is UNMODIFIED. */
#include <string.h>
void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d=(unsigned char *)dst;
  const unsigned char *s=(const unsigned char *)src;
  for (size_t i=0;i<n;i++) d[i]=s[i];
  return dst;
}
void *memset(void *dst, int c, size_t n) {
  unsigned char *d=(unsigned char *)dst;
  for (size_t i=0;i<n;i++) d[i]=(unsigned char)c;
  return dst;
}

/* ISO C overlap-safe memmove for the cross-ISA freestanding runtime.
 * AArch64 TinyCC correctly emits a memmove dependency from real zlib,
 * even though x86 did not; provide one portable implementation. */
void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d=(unsigned char *)dst;
  const unsigned char *s=(const unsigned char *)src;
  unsigned long da=(unsigned long)dst, sa=(unsigned long)src;
  if (da<=sa || da-sa>=n) {
    for (size_t i=0;i<n;i++) d[i]=s[i];
  } else {
    while (n) { --n; d[n]=s[n]; }
  }
  return dst;
}
