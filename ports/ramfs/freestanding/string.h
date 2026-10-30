#ifndef NASEED_RAMFS_PORT_STRING_H
#define NASEED_RAMFS_PORT_STRING_H
#include <stddef.h>
#include "../../../kernel/lib/string.h"
char *nas_ramfs_strdup(const char *);
char *nas_ramfs_strndup(const char *,size_t);
#define strdup nas_ramfs_strdup
#define strndup nas_ramfs_strndup
#endif
