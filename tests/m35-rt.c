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
