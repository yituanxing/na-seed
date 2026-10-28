#ifndef NASEED_RAMFS_PORT_ERRNO_H
#define NASEED_RAMFS_PORT_ERRNO_H
#include "../../../kernel/errno.h"
extern int nas_ramfs_errno;
#define errno nas_ramfs_errno
#endif
