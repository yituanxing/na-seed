#ifndef NASEED_FS_OPEN_PATH_H
#define NASEED_FS_OPEN_PATH_H
#include <stdint.h>
/* Internal open helper shared by openat and legacy fstatat fallback. */
uint64_t nas_fs_open_path(const char *path, uint64_t flags, uint64_t mode);
#endif
