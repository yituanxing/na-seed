#ifndef NASEED_TASK_USER_STACK_H
#define NASEED_TASK_USER_STACK_H
#include <stdbool.h>
#include <stdint.h>
/* Bounded common Linux exec argv/env ABI shared by all three ISAs.
 * Real upstream BusyBox Kbuild exports >32 variables through recursive GNU
 * Make, revealing the obsolete 32-entry env limit on every architecture.
 * Accept 128 env entries. The 1-MiB eager user stack remains an explicit
 * interim bound (not Linux grow-down/demand paging); user_stack.c checks
 * actual string/pointer usage, NOT a fictitious worst-case assumption that
 * all 64 argv and 128 env strings are exactly 8191 bytes.
 * One owner for this same stack extent in boot, exec and auxv creation. */
#define NAS_USER_STACK_PAGES 256U
#define NAS_ARGV_MAX 64
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
