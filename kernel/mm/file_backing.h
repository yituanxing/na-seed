#ifndef NASEED_MM_FILE_BACKING_H
#define NASEED_MM_FILE_BACKING_H
#include <stdint.h>
#include <stdbool.h>
/* Shared file-backed VM speaks only an opaque, inode-owned capability.
 * The VFS adapter implements these operations; MM must never depend on
 * a concrete tmpfs/ext4 inode, descriptor table or Linux syscall ABI. */
struct nas_mapped_file;
struct nas_fs_node;
int nas_fs_mmap_pin(uint64_t fd,bool writable,struct nas_mapped_file **out);
/* An immutable file's VFS inode can be pinned without a writable shared
 * page-cache provider. Private lazy faults read fresh independent frames. */
int nas_fs_mmap_pin_immutable(uint64_t fd,struct nas_mapped_file **out);
int64_t nas_fs_mmap_read_immutable(struct nas_mapped_file *file,
                                    uint64_t offset,uint8_t *dst,uint64_t len);
bool nas_fs_mmap_get(struct nas_mapped_file *file);
void nas_fs_mmap_put(struct nas_mapped_file *file);
bool nas_fs_mmap_writable(const struct nas_mapped_file *file);
/* Acquire a separately reference-counted physical frame for ONE PTE.
 * Success transfers that reference to the caller, which must put it if
 * page-table installation fails. The inode owns its own storage frame. */
int nas_fs_mmap_page(struct nas_mapped_file *file,uint64_t file_offset,
                     uint64_t *pa_owned);
/* Read-only residency probe: no allocations, no new frame references. */
int nas_fs_mmap_page_resident(struct nas_mapped_file *file,uint64_t file_offset);
int nas_fs_mmap_dirty(struct nas_mapped_file *file);
/* precommit VMA replacement uses rearm=false to preserve rollback PTEs;
 * successful user msync uses true to trap the next real store for mtime. */
int nas_fs_mmap_sync(struct nas_mapped_file *file,bool rearm);
bool nas_fs_mmap_matches(struct nas_mapped_file *file,
                          const struct nas_fs_node *node);
#endif
