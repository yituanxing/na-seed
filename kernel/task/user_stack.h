#ifndef NASEED_TASK_USER_STACK_H
#define NASEED_TASK_USER_STACK_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_ARGV_MAX 12
#define NAS_ENV_MAX 16
bool nas_parse_boot_argv(const char *cmdline, const char *const **args, unsigned *argc);
bool nas_boot_argv_enabled(void);
const char *nas_boot_argv_first(void);
bool nas_build_user_stack(const char *const *args, unsigned argc,
                          const char *const *envp, unsigned envc,
                          const char *execfn, uint64_t *out_sp);
bool nas_build_linux_stack(bool bash, bool busybox, uint64_t *out_sp);
#endif
