#ifndef NASEED_TASK_USER_STACK_H
#define NASEED_TASK_USER_STACK_H
#include <stdbool.h>
#include <stdint.h>
/* Bounded common Linux exec argument vector, shared by all three ISAs.
 * Real upstream GNU Make may pass >12 object files to the linker.
 * 64 argv × 256 bytes + 32 env × 256 fits in the mapped 128-KiB stack. */
#define NAS_ARGV_MAX 64
#define NAS_ENV_MAX 32
bool nas_parse_boot_argv(const char *cmdline, const char *const **args, unsigned *argc);
bool nas_boot_argv_enabled(void);
const char *nas_boot_argv_first(void);
bool nas_build_user_stack(const char *const *args, unsigned argc,
                          const char *const *envp, unsigned envc,
                          const char *execfn, uint64_t *out_sp);
bool nas_build_linux_stack(bool bash, bool busybox, uint64_t *out_sp);
#endif
