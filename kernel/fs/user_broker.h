#ifndef NASEED_FS_USER_BROKER_H
#define NASEED_FS_USER_BROKER_H
#include <stdbool.h>
#include <stdint.h>
#include "../syscall/ops.h"
struct nas_fs_user_broker_answer { bool handled; uint64_t value; };
#ifdef NAS_ENABLE_M180_FS_USER_IRQ
/* Optional native syscall boundary. This is the ONLY point at which a
 * user Task gives up its kernel continuation to an IRQ-backed FS worker. */
struct nas_fs_user_broker_answer nas_fs_user_broker_enter(
    uint64_t *frame,enum nas_syscall_op nr,const uint64_t *args);
#else
static inline struct nas_fs_user_broker_answer nas_fs_user_broker_enter(
    uint64_t *frame,enum nas_syscall_op nr,const uint64_t *args) {
    (void)frame;(void)nr;(void)args;
    return (struct nas_fs_user_broker_answer){false,0};
}
#endif
#endif
