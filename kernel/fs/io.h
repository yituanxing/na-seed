#ifndef NASEED_FS_IO_H
#define NASEED_FS_IO_H
#include <stdint.h>
#include <stdbool.h>
struct nas_task;
/* FS/FD operations are implemented in fs/syscall.c; the shared syscall
 * dispatcher and scheduler use this stable operation boundary. */
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t addr, uint64_t len);
/* Kernel-buffer, position-independent file reads for MAP_PRIVATE.
 * A CPIO/tmpfs regular file is the source of truth; FD offsets are untouched.
 * Negative Linux errno on invalid or unsupported file kinds. */
int64_t nas_fs_mmap_read(uint64_t fd, uint64_t offset,
                        uint8_t *buffer, uint64_t length);
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
