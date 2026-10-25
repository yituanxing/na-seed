#ifndef NASEED_FS_NAMESPACE_SYSCALL_H
#define NASEED_FS_NAMESPACE_SYSCALL_H
#include "syscall.h"
/* Shared namespace and pathname ABI. Return handled=false outside this module's syscall set. */
struct nas_fs_result nas_fs_namespace_syscall(enum nas_syscall_op op,
                                           const uint64_t *args);
#endif
