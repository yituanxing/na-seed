#ifndef NASEED_ARCH_MMU_SELECT_H
#define NASEED_ARCH_MMU_SELECT_H
#if defined(__x86_64__)
#include "x86_64/mmu.h"
#else
#error "NaSeed MMU backend not implemented"
#endif
#endif
