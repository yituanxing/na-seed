#ifndef NASEED_TASK_USER_STACK_H
#define NASEED_TASK_USER_STACK_H
#include <stdbool.h>
#include <stdint.h>
/* Bounded common Linux exec argument vector, shared by all three ISAs.
 * Real upstream GNU Make may pass >12 object files to the linker.
 * 64 argv x 256 bytes + 32 env x 256 fits in the mapped stack.
 * A fixed 1-MiB, eager, per-process main stack is an explicit bounded
 * interim policy (not Linux stack grow-down/demand paging). The previous
 * 128-KiB stack faulted running original Ninja on all 3 target ISAs.
 * One owner for the same stack extent in boot, exec and auxv creation. */
#define NAS_USER_STACK_PAGES 256U
#define NAS_ARGV_MAX 64
#define NAS_ENV_MAX 32
bool nas_user_stack_map_initial(void);
bool nas_parse_boot_argv(const char *cmdline, const char *const **args, unsigned *argc);
bool nas_boot_argv_enabled(void);
const char *nas_boot_argv_first(void);
bool nas_build_user_stack(const char *const *args, unsigned argc,
                          const char *const *envp, unsigned envc,
                          const char *execfn, uint64_t *out_sp);
bool nas_build_linux_stack(bool bash, bool busybox, uint64_t *out_sp);
#endif
