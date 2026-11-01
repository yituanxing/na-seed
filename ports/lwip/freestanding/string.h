#ifndef NASEED_LWIP_FREESTANDING_STRING_H
#define NASEED_LWIP_FREESTANDING_STRING_H
/* Only selected when building the original lwIP sources for a none-elf
 * kernel. The shared NaSeed freestanding string provider remains the owner. */
#include "../../../kernel/lib/string.h"
char *nas_lwip_strstr(const char *haystack,const char *needle);
#define strstr nas_lwip_strstr
#endif
