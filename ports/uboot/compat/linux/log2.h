#ifndef NAS_UBOOT_LOG2_H
#define NAS_UBOOT_LOG2_H
#include "../base.h"
static inline bool is_power_of_2(unsigned long x){return x && !(x&(x-1));}
#define LOG2(x) ((unsigned)(31-__builtin_clz((unsigned)(x))))
#endif
