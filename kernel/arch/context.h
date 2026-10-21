#ifndef NASEED_CONTEXT_SELECT_H
#define NASEED_CONTEXT_SELECT_H
#if defined(__x86_64__)
#include "x86_64/context.h"
#else
#error "NaSeed task context backend not implemented"
#endif
#endif
