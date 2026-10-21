#ifndef NASEED_CONTEXT_SELECT_H
#define NASEED_CONTEXT_SELECT_H
#include <stdint.h>
#if defined(__x86_64__)
#include "x86_64/context.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "riscv64/context.h"
#elif defined(__aarch64__)
#include "aarch64/context.h"
#else
#error "NaSeed task context backend not implemented"
#endif
/* Task lifecycle cannot assume FSBASE is how every ISA represents TLS. */
static inline uint64_t nas_arch_context_tls_base(const struct nas_arch_context *c) {
#if defined(__x86_64__)
    return c->user_fs_base;
#else
    return c->user_tls_base;
#endif
}
static inline void nas_arch_context_set_tls(struct nas_arch_context *c, uint64_t value) {
#if defined(__x86_64__)
    c->user_fs_base = value;
#else
    c->user_tls_base = value;
#endif
}
#endif
