#ifndef NASEED_LWIP_ERRNO_BRIDGE_H
#define NASEED_LWIP_ERRNO_BRIDGE_H
/* A single kernel/Linux errno registry; no lwIP-owned duplicate values. */
#include "../../../kernel/errno.h"
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#endif
