#ifndef NASEED_FS_IO_H
#define NASEED_FS_IO_H
#include <stdint.h>
/* Temporary boundary: working FD adapters remain implemented in m1.c. */
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t addr, uint64_t len);
uint64_t nas_fs_fd_write(uint64_t fd, uint64_t addr, uint64_t len);
uint64_t nas_fs_fd_writev(uint64_t fd, uint64_t iov, uint64_t n);
void nas_fs_close_fd(uint64_t fd);
#endif
