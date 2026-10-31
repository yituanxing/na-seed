#ifndef NASEED_FS_IO_H
#define NASEED_FS_IO_H
#include <stdint.h>
#include <stdbool.h>
#include "../mm/file_backing.h"
struct nas_task;
struct nas_pending_io;
struct nas_fs_node;
/* One issuing syscall may park after a provider reports would-block.
 * The file layer owns the imported iovec and an independent File ref;
 * scheduler/Task carry only this opaque request during the native C wait. */
struct nas_io_attempt {
    struct nas_pending_io *pending;
    uint64_t result,tty_requested_bytes;
    bool pipe_like;
};
void nas_fs_io_start(uint64_t op,uint64_t fd,uint64_t address,uint64_t length,
                     struct nas_io_attempt *attempt);
uint64_t nas_fs_io_resume(struct nas_pending_io *pending);
bool nas_fs_io_should_block(const struct nas_pending_io *pending,
                            uint64_t result,bool first_attempt);
bool nas_fs_io_ready(const struct nas_pending_io *pending,
                     const struct nas_task *task);
bool nas_fs_io_pipe_like(const struct nas_pending_io *pending);
bool nas_fs_io_tty_read(const struct nas_pending_io *pending);
void nas_fs_io_dispose(struct nas_pending_io *pending);
/* FS/FD operations are implemented in fs/syscall.c; the shared syscall
 * dispatcher and scheduler use this stable operation boundary. */
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t addr, uint64_t len);
uint64_t nas_fs_fd_readv(uint64_t fd, uint64_t iov, uint64_t count);
/* A blocking readv's true byte count belongs to its issuing Task, not a
 * guessed iovcnt or a user pointer read from another Task's address space. */
uint64_t nas_fs_fd_readv_with_size(uint64_t fd, uint64_t iov, uint64_t count,
                                  uint64_t *requested_out);
/* Linux positional regular-file I/O never advances shared FD position. */
uint64_t nas_fs_fd_pread(uint64_t fd,uint64_t address,uint64_t count,uint64_t offset);
uint64_t nas_fs_fd_pwrite(uint64_t fd,uint64_t address,uint64_t count,uint64_t offset);
/* Kernel-buffer, position-independent file reads for file mmap.
 * A CPIO/tmpfs regular file is the source of truth; FD offsets are untouched.
 * Negative Linux errno on invalid or unsupported file kinds. */
int64_t nas_fs_mmap_read(uint64_t fd, uint64_t offset,
                        uint8_t *buffer, uint64_t length);
/* 1 iff an open readable regular file's bytes cannot change in this boot;
 * 0 for mutable/dynamic sources, negative errno for an invalid descriptor.
 * Read-only mounts alone do not imply immutable data. */
int nas_fs_mmap_immutable_readonly(uint64_t fd);
uint64_t nas_fs_fd_write(uint64_t fd, uint64_t addr, uint64_t len);
uint64_t nas_fs_fd_writev(uint64_t fd, uint64_t iov, uint64_t n);
void nas_fs_close_fd(uint64_t fd);
/* pollfd wire format stays in the FS adapter; kernel-space wait snapshots
 * can be checked without switching to another task's address space. */
uint64_t nas_fs_poll(uint64_t pollfds,uint64_t nfds,uint64_t timeout_ms);
uint64_t nas_fs_ppoll(uint64_t pollfds,uint64_t nfds,
                      uint64_t timeout_ptr,uint64_t sigmask_ptr);
uint64_t nas_fs_pselect6(uint64_t nfds, uint64_t readfds,
                        uint64_t writefds, uint64_t exceptfds,
                        bool immediate);
bool nas_fs_pselect_arm(uint64_t nfds, uint64_t readfds,
                        uint64_t writefds, uint64_t exceptfds);
bool nas_fs_poll_arm(uint64_t pollfds,uint64_t nfds);
bool nas_fs_poll_ready(const struct nas_task *task);
#endif
