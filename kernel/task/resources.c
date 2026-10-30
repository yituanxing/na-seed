#include "resources.h"
#include "../task.h"
#include "../fs/mode.h"
#include "../panic.h"
#include "../lib/string.h"
#include "../fs/vfs.h"

/* Single-CPU bounded storage policy, independent of Task/leader identity. */
static struct nas_fs_context fs_pool[NAS_TASK_LIMIT * 2U];
static struct nas_sighand sighand_pool[NAS_TASK_LIMIT * 2U];

void nas_resources_system_init(void) {
    memset(fs_pool,0,sizeof(fs_pool));
    memset(sighand_pool,0,sizeof(sighand_pool));
}
struct nas_fs_context *nas_fs_context_new(void) {
    for(unsigned i=0;i<NAS_TASK_LIMIT * 2U;i++) {
        struct nas_fs_context *fs=&fs_pool[i];
        if(fs->refs) continue;
        memset(fs,0,sizeof(*fs));
        fs->refs=1;
        fs->umask=NAS_UMASK_DEFAULT;
        fs->cwd[0]='/';
        return fs;
    }
    return 0;
}
struct nas_fs_context *nas_fs_context_copy(const struct nas_fs_context *src) {
    if(!src || !src->refs) nas_fail("FS context copy without owner");
    struct nas_fs_context *fs=nas_fs_context_new();
    if(!fs) return 0;
    fs->umask=src->umask;
    memcpy(fs->cwd,src->cwd,sizeof(fs->cwd));
    if(src->cwd_pinned) {
        if(nas_vfs_node_pin(&src->cwd_node)) {
            nas_fs_context_put(fs);
            return 0;
        }
        fs->cwd_node=src->cwd_node;
        fs->cwd_pinned=true;
    }
    if(src->cwd_path.dentry &&
       nas_vfs_path_get(&src->cwd_path,&fs->cwd_path)) {
        nas_fs_context_put(fs);
        return 0;
    }
    return fs;
}
struct nas_fs_context *nas_fs_context_get(struct nas_fs_context *fs) {
    if(!fs || !fs->refs || fs->refs==UINT32_MAX)
        nas_fail("FS context invalid get");
    fs->refs++;
    return fs;
}
void nas_fs_context_put(struct nas_fs_context *fs) {
    if(!fs || !fs->refs) nas_fail("FS context invalid put");
    if(!--fs->refs) {
        if(fs->cwd_path.dentry && nas_vfs_path_put(&fs->cwd_path))
            nas_fail("FS cwd dentry pin corrupted");
        if(fs->cwd_pinned && nas_vfs_node_unpin(&fs->cwd_node))
            nas_fail("FS cwd mount pin corrupted");
        memset(fs,0,sizeof(*fs));
    }
}
struct nas_sighand *nas_sighand_new(void) {
    for(unsigned i=0;i<NAS_TASK_LIMIT * 2U;i++) {
        struct nas_sighand *s=&sighand_pool[i];
        if(s->refs) continue;
        memset(s,0,sizeof(*s));
        s->refs=1;
        return s;
    }
    return 0;
}
struct nas_sighand *nas_sighand_copy(const struct nas_sighand *src) {
    if(!src || !src->refs) nas_fail("signal dispositions copy without owner");
    struct nas_sighand *s=nas_sighand_new();
    if(!s) return 0;
    memcpy(s->actions,src->actions,sizeof(s->actions));
    return s;
}
struct nas_sighand *nas_sighand_get(struct nas_sighand *s) {
    if(!s || !s->refs || s->refs==UINT32_MAX)
        nas_fail("signal dispositions invalid get");
    s->refs++;
    return s;
}
void nas_sighand_put(struct nas_sighand *s) {
    if(!s || !s->refs) nas_fail("signal dispositions invalid put");
    if(!--s->refs) memset(s,0,sizeof(*s));
}
