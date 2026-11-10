/* Legacy focused host VFS tests do not initialize process Files or Linux
 * syscalls. Keep their VFS provider fixtures independent of M187's native
 * event queue; the real inotify provider has a separate executable test. */
#include <stdint.h>
#include <stdbool.h>
#include "../kernel/fs/inotify.h"
#include "../kernel/task.h"
void nas_inotify_emit_child(const struct nas_fs_node *p,const char *name,
                           uint32_t mask,uint32_t cookie,bool isdir) {
    (void)p;(void)name;(void)mask;(void)cookie;(void)isdir;
}
void nas_inotify_emit_node(const struct nas_fs_node *n,uint32_t mask) {
    (void)n;(void)mask;
}
void nas_inotify_erase_node(const struct nas_fs_node *n) {(void)n;}
void nas_inotify_erase_mount(const void *mount,uint64_t id) {
    (void)mount;(void)id;
}
uint32_t nas_inotify_rename_cookie(void) {return 1;}

/* This isolated host lwext4/VFS fixture exercises only the explicitly
 * registered ext4 mount. Production NaSeed links vfs_providers.c and mounts
 * its real procfs/tmpfs/bootfs providers; loading them into this tiny host
 * fixture would drag in unrelated task, tty and syscall dependencies. */
/* Some host tests also link the real provider file: its strong definition
 * must win instead of causing a duplicate-symbol link failure. */
__attribute__((weak)) int nas_vfs_register_pseudo_mounts(void) {return 0;}

/* M253 DAC looks up the active Task for every path. Legacy host VFS-only
 * fixtures intentionally have no scheduler and exercise the bootstrap/root
 * path; expose a WEAK null current-task pointer only in this host stub.
 * Production kernel task.c provides the real strong pointer. */
__attribute__((weak)) struct nas_task *nas_current_task;
