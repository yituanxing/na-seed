#ifndef NAS_TLSF_ASSERT_H
#define NAS_TLSF_ASSERT_H
#define assert(expr) do { if (!(expr)) __builtin_trap(); } while (0)
#endif
