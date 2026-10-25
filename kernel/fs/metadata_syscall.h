#ifndef NASEED_FS_METADATA_SYSCALL_H
#define NASEED_FS_METADATA_SYSCALL_H
#include "syscall.h"
struct nas_fs_result nas_fs_metadata_syscall(enum nas_syscall_op op, const uint64_t *args);
#endif
