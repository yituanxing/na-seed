#ifndef NASEED_KERNEL_UTS_H
#define NASEED_KERNEL_UTS_H
#include <stdbool.h>
#include <stddef.h>

/* Kernel-wide UTS nodename, with no UTS namespaces yet. Linux permits
 * 64 name bytes plus a terminating NUL in struct utsname. */
#define NAS_UTS_NODENAME_MAX 64U
bool nas_uts_set_hostname(const char *hostname,size_t length);
void nas_uts_copy_hostname(char output[NAS_UTS_NODENAME_MAX+1]);

#endif
