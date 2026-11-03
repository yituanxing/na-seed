/* Legacy focused host VFS tests do not initialize process Files or Linux
 * syscalls. Keep their VFS provider fixtures independent of M187's native
 * event queue; the real inotify provider has a separate executable test. */
#include <stdint.h>
#include <stdbool.h>
#include "../kernel/fs/inotify.h"
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
