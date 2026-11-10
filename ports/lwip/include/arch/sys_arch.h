#ifndef NASEED_LWIP_ARCH_SYS_ARCH_H
#define NASEED_LWIP_ARCH_SYS_ARCH_H
/* Exactly one native kernel scheduler/queue implementation: no lwIP-side
 * thread table, timer wheel, wait queue or mailbox ring. */
#include "../../../../kernel/task/kthread.h"
#include <stdint.h>
struct nas_lwip_sem;
struct nas_lwip_mbox;
typedef struct nas_lwip_sem *sys_sem_t;
typedef struct nas_lwip_mbox *sys_mbox_t;
typedef struct nas_kthread_handle sys_thread_t;
typedef uint32_t sys_prot_t;
sys_prot_t sys_arch_protect(void);
void sys_arch_unprotect(sys_prot_t);
#endif
