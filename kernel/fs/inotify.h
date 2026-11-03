#ifndef NASEED_FS_INOTIFY_H
#define NASEED_FS_INOTIFY_H
#include <stdbool.h>
#include <stdint.h>
struct nas_fs_node;
/* Native LP64 Linux inotify, shared across x86-64, RV64, and AArch64. */
#define NAS_IN_ACCESS       0x00000001U
#define NAS_IN_MODIFY       0x00000002U
#define NAS_IN_ATTRIB       0x00000004U
#define NAS_IN_CLOSE_WRITE  0x00000008U
#define NAS_IN_CLOSE_NOWRITE 0x00000010U
#define NAS_IN_OPEN         0x00000020U
#define NAS_IN_MOVED_FROM   0x00000040U
#define NAS_IN_MOVED_TO     0x00000080U
#define NAS_IN_CREATE       0x00000100U
#define NAS_IN_DELETE       0x00000200U
#define NAS_IN_DELETE_SELF  0x00000400U
#define NAS_IN_MOVE_SELF    0x00000800U
#define NAS_IN_Q_OVERFLOW   0x00004000U
#define NAS_IN_IGNORED      0x00008000U
#define NAS_IN_ISDIR        0x40000000U
uint64_t nas_inotify_init1(uint64_t flags);
uint64_t nas_inotify_add_watch(uint64_t fd,uint64_t user_path,uint64_t mask);
uint64_t nas_inotify_rm_watch(uint64_t fd,uint64_t wd);
uint64_t nas_inotify_read_plan(unsigned slot,uint64_t count);
uint64_t nas_inotify_read(unsigned slot,uint8_t *dst,uint64_t count);
int nas_inotify_pending_bytes(unsigned slot,uint32_t *bytes);
uint16_t nas_inotify_poll_slot(unsigned slot,uint16_t events);
uint16_t nas_inotify_poll_fd(uint64_t fd,uint16_t events);
void nas_inotify_release(unsigned slot);
void nas_inotify_emit_child(const struct nas_fs_node *parent,const char *name,
                           uint32_t mask,uint32_t cookie,bool is_directory);
void nas_inotify_emit_node(const struct nas_fs_node *node,uint32_t mask);
void nas_inotify_erase_node(const struct nas_fs_node *node);
void nas_inotify_erase_mount(const void *mount,uint64_t mount_id);
uint32_t nas_inotify_rename_cookie(void);
#endif
