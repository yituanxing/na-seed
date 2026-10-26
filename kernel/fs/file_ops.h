#ifndef NASEED_FS_FILE_OPS_H
#define NASEED_FS_FILE_OPS_H
#include <stdint.h>
#include "../task.h"
/* Small, shared open-description operation table. This is NOT a generic VFS;
 * pathname traversal, inode metadata and the future mount tree stay separate.
 * Backend callbacks still use validated user addresses at the syscall edge. */
struct nas_file_ops {
    uint64_t (*read)(uint64_t fd,uint64_t user_addr,uint64_t count);
    uint64_t (*write)(uint64_t fd,uint64_t user_addr,uint64_t count);
    uint16_t (*poll)(uint64_t fd,uint16_t events);
    /* Readiness without a process-local FD or active userspace root.
     * Used by epoll, blocked tasks and future socket/device providers.
     * slot is an owned open-file description, not an integer user FD. */
    uint16_t (*poll_description)(unsigned slot,uint16_t events);
    uint64_t (*ioctl)(uint64_t fd,uint64_t cmd,uint64_t arg);
    void (*release)(unsigned slot);
};
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind);
/* Call a live open-file description's operation, not an FD kind switch. */
uint16_t nas_fs_poll_description(unsigned slot,uint16_t events);
#endif
