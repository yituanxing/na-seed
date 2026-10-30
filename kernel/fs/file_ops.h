#ifndef NASEED_FS_FILE_OPS_H
#define NASEED_FS_FILE_OPS_H
#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
/* Shared open-description data plane. A provider only sees an owned File slot
 * and kernel memory: Linux FDs and user virtual addresses belong exclusively
 * to the syscall adapter in file_io.c. This is not a pathname/VFS interface.
 *
 * Optional plans are non-consuming and return a transfer budget or negative
 * errno. Call them ONCE for the original logical read/write request, before
 * splitting usercopy into bounded page-local chunks. Providers define their
 * own admission semantics: pipes reserve <=PIPE_BUF writes atomically, while
 * stream sockets may admit a short write when only some space remains.
 * A provider may opt into read_finalize_zero to retire a zero-length EOF
 * token when read_plan returned 0 for a nonempty user read (PTY canonical
 * EOF). A user's read(fd,...,0) never retires this token.
 */
struct nas_file_ops {
    uint64_t (*read_plan)(unsigned slot,uint64_t requested);
    uint64_t (*write_plan)(unsigned slot,uint64_t requested);
    uint64_t (*read)(unsigned slot,uint8_t *kernel_dst,uint64_t count);
    uint64_t (*write)(unsigned slot,const uint8_t *kernel_src,uint64_t count);
    /* An admitted atomic write must validate the whole user source before
     * the first provider commit; 0 means ordinary page-local staging. */
    uint64_t atomic_write_limit;
    /* Linux eventfd is exceptional: it consumes the counter before copying
     * the value out, even when usercopy fails with EFAULT. Other providers
     * preflight writable user memory BEFORE consuming any data. */
    bool read_commit_before_copyout;
    bool read_finalize_zero;
    /* Null-like sinks ignore source PAGE CONTENTS: Linux accepts unmapped
     * source pages for /dev/null writes, but a non-userspace address range
     * still fails the common access_ok check before provider dispatch. */
    bool write_ignores_source;
    /* An iter-style provider sees one logical readv/writev request, across
     * all iovecs. Legacy scalar-only providers instead receive individual
     * segments (eventfd is read_iter but scalar-write only on Linux). */
    bool aggregate_readv;
    bool aggregate_writev;
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
