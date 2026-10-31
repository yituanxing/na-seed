#ifndef NASEED_TASK_USER_STACK_H
#define NASEED_TASK_USER_STACK_H
#include <stdbool.h>
#include <stdint.h>
/* Bounded common Linux exec argv/env ABI shared by all three ISAs.
 * Real upstream BusyBox Kbuild exports >32 variables through recursive GNU
 * Make, revealing the obsolete 32-entry env limit on every architecture.
 * Accept 128 env entries. Retain a 1-MiB eagerly mapped stack top for exec
 * argument staging, while reserving an 8-MiB writable stack VMA: absent
 * lower pages materialize on first touch through the shared MM fault path.
 * user_stack.c checks
 * actual string/pointer usage, NOT a fictitious worst-case assumption that
 * all 64 argv and 128 env strings are exactly 8191 bytes.
 * One owner for these stack extents in boot, exec and auxv creation. */
#define NAS_USER_STACK_PAGES 256U
#define NAS_USER_STACK_RESERVE_PAGES 2048U
/* Genuine upstream Kbuild expands hundreds of source filenames into sed
 * argv. Bound aggregate exec strings separately from the vector count. */
#define NAS_ARGV_MAX 2048
#define NAS_ENV_MAX 128
bool nas_user_stack_map_initial(void);
bool nas_parse_boot_argv(const char *cmdline, const char *const **args, unsigned *argc);
bool nas_boot_argv_enabled(void);
const char *nas_boot_argv_first(void);
bool nas_build_user_stack(const char *const *args, unsigned argc,
                          const char *const *envp, unsigned envc,
                          const char *execfn, uint64_t *out_sp);
bool nas_build_linux_stack(bool bash, bool busybox, uint64_t *out_sp);
#endif
