#ifndef NAS_TLSF_STDIO_H
#define NAS_TLSF_STDIO_H
/* Upstream diagnostics only; failures still propagate and assertions trap. */
static inline int printf(const char *format, ...) { (void)format; return 0; }
#endif
