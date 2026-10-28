#ifndef NAS_LITTLEFS_FREESTANDING_STRING_H
#define NAS_LITTLEFS_FREESTANDING_STRING_H
/* No hosted glibc headers in cross-ISA kernel code; reuse kernel primitives. */
#include "../../../../kernel/lib/string.h"
#endif
