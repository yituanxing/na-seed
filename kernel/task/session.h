#ifndef NASEED_TASK_SESSION_H
#define NASEED_TASK_SESSION_H
#include <stdint.h>
/* Shared Linux POSIX process-group/session policy, not architecture ABI.
 * Terminal controlling foreground group and TTY job control are later.
 */
uint64_t nas_session_getpgid(uint64_t raw_pid);
uint64_t nas_session_getsid(uint64_t raw_pid);
uint64_t nas_session_setpgid(uint64_t raw_pid,uint64_t raw_pgid);
uint64_t nas_session_setsid(void);
#endif
