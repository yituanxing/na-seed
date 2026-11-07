#ifndef NASEED_FS_FILE_OPS_H
#define NASEED_FS_FILE_OPS_H
#include <stdint.h>
#include <stdbool.h>
#include "../task.h"
#include "../errno.h"
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
/* Provider-owned blocking policy, independent of FD-kind dispatch. */
#define NAS_IO_WAIT_INITIAL 0x01U
#define NAS_IO_WAIT_RETRY   0x02U
#define NAS_IO_WAIT_TTY     0x04U
/* PTY (and other explicitly opting-in streams) reports ordinary -EAGAIN on
 * its first empty blocking read. Pipe/socket initial admission deliberately
 * retains its existing -EOPNOTSUPP-only policy. */
#define NAS_IO_WAIT_INITIAL_EAGAIN 0x08U
static inline bool nas_io_wait_policy(uint8_t flags,bool nonblocking,
                                      int64_t result,bool initial) {
    if(nonblocking)return false;
    if(initial)
        return ((flags&NAS_IO_WAIT_INITIAL) && result==-EOPNOTSUPP) ||
               ((flags&NAS_IO_WAIT_INITIAL_EAGAIN) && result==-EAGAIN);
    return (flags&NAS_IO_WAIT_RETRY) &&
           (result==-EOPNOTSUPP || result==-EAGAIN);
}
struct nas_file_ops {
    uint64_t (*read_plan)(unsigned slot,uint64_t requested);
    /* A record-oriented provider (evdev) must never receive half of one
     * input_event because a user iovec or page boundary split a transfer. */
    uint16_t read_record_bytes;
    /* Variable-length native records (inotify_event) must cross virtual
     * page boundaries in one prepared usercopy, never split the wire record. */
    bool read_variable_records;
    uint64_t (*write_plan)(unsigned slot,uint64_t requested);
    uint64_t (*read)(unsigned slot,uint8_t *kernel_dst,uint64_t count);
    /* Optional non-consuming stream snapshot. offset is relative to the
     * current read cursor; the caller does usercopy in bounded chunks.
     * Only a provider with a real retained RX buffer advertises this. */
    uint64_t (*peek)(unsigned slot,uint8_t *kernel_dst,uint64_t count,
                     uint64_t offset);
    uint64_t (*write)(unsigned slot,const uint8_t *kernel_src,uint64_t count);
    /* Optional explicit-offset I/O for seekable character devices.  Like
     * VFS read_at/write_at, these callbacks neither consult nor mutate the
     * shared open-file position (including across dup/fork).  User pointers
     * and descriptor access policy remain in file_io.c, never in a driver. */
    uint64_t (*read_at)(unsigned slot,uint64_t offset,
                        uint8_t *kernel_dst,uint64_t count);
    uint64_t (*write_at)(unsigned slot,uint64_t offset,
                         const uint8_t *kernel_src,uint64_t count);
    /* An admitted atomic write must validate the whole user source before
     * the first provider commit; 0 means ordinary page-local staging. */
    uint64_t atomic_write_limit;
    /* Counter FDs (eventfd/timerfd) consume their prepared expiration count
     * before copying it out; other providers preflight writable user memory
     * BEFORE consuming any data. */
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
    /* Block/retry is a File capability, not a syscall-layer FD-kind list.
     * Distinct bits retain Linux admission/retry semantics: an initially
     * parked request may race with another consumer after becoming ready. */
    uint8_t io_wait_flags;
    /* Task-owned blocking read policy, e.g. termios VMIN/VTIME on PTY.
     * FileOps owns the descriptor identity; the scheduler owns monotonic
     * deadlines. The provider must never inspect another Task's user VM. */
    void (*read_wait_start)(unsigned slot,struct nas_task *task,
                            uint64_t requested);
    bool (*read_wait_ready)(unsigned slot,struct nas_task *task,
                            uint64_t requested);
    bool sends_sigpipe;
    uint16_t (*poll)(uint64_t fd,uint16_t events);
    /* Readiness without a process-local FD or active userspace root.
     * Used by epoll, blocked tasks and future socket/device providers.
     * slot is an owned open-file description, not an integer user FD. */
    uint16_t (*poll_description)(unsigned slot,uint16_t events);
    /* Optional Task-dependent readiness (signalfd). Scheduler passes the
     * BLOCKED owner, never infers it from the currently running process. */
    uint16_t (*poll_description_task)(unsigned slot,uint16_t events,
                                      const struct nas_task *task);
    uint64_t (*ioctl)(uint64_t fd,uint64_t cmd,uint64_t arg);
    void (*release)(unsigned slot);
};
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind);
/* Call a live open-file description's operation, not an FD kind switch. */
uint16_t nas_fs_poll_description(unsigned slot,uint16_t events);
/* Scheduler probes an arbitrary blocked Task; signal-backed descriptors
 * must inspect that Task's pending mask, not the currently running Task. */
struct nas_task;
uint16_t nas_fs_poll_description_task(unsigned slot,uint16_t events,
                                     const struct nas_task *task);
uint16_t nas_fs_signalfd_poll_task(unsigned slot,uint16_t events,
                                  const struct nas_task *task);
#endif
