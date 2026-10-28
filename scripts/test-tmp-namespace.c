/* M63 host test of REAL tmpfs inode and namespace implementations.
 * Small deterministic bootfs, RAM frame and time providers; no fake VFS
 * algorithm and no need to compile third-party programs inside QEMU. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/tmpfs.h"
#include "../kernel/bootfs.h"
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"

#define FRAMES 80
static _Alignas(4096) uint8_t frames[FRAMES][4096];
static bool busy[FRAMES];
static unsigned frame_allocated,frame_freed;
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<FRAMES;i++)
        if(!busy[i]) {
            busy[i]=true;frame_allocated++;
            memset(frames[i],0,4096);
            return (uint64_t)(uintptr_t)&frames[i][0];
        }
    return 0;
}
bool nas_frame_put(uint64_t phys) {
    for(unsigned i=0;i<FRAMES;i++)
        if(phys==(uint64_t)(uintptr_t)&frames[i][0] && busy[i]) {
            busy[i]=false;frame_freed++;return true;
        }
    return false;
}
uint64_t nas_hhdm_offset(void) {return 0;}
static uint64_t fake_tick;
uint64_t nas_arch_monotonic_ns(void) {return ++fake_tick;}
bool nas_time_realtime_ns(uint64_t *now) {(void)now;return false;}

static struct bootfs_entry root_tmp={.path="/tmp",.mode=0040755U,.ino=1};
static struct bootfs_entry root_ro={.path="/ro",.mode=0100444U,.ino=2};
bool bootfs_mounted(void) {return true;}
const struct bootfs_entry *bootfs_find(const char *path) {
    if(!strcmp(path,"/tmp"))return &root_tmp;
    if(!strcmp(path,"/ro"))return &root_ro;
    return 0;
}
bool bootfs_is_dir(const struct bootfs_entry *e) {
    return e && (e->mode & 0170000U)==0040000U;
}
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
/* M63 links the actual tmpfs data/namespace objects without VFS to keep
 * its namespace test isolated. Model only the VFS owner signal here; the
 * separate generic VFS fixture tests the REAL refcounted lease table. */
static const void *borrowed_vfs_pin;
bool nas_vfs_inode_is_pinned(const void *identity) {
    return identity && identity==borrowed_vfs_pin;
}
static struct nas_task cwd_task;
static struct nas_fs_context cwd_fs;
struct nas_task *nas_task_at(unsigned n) {
    return n==0 ? &cwd_task : 0;
}
static void putcwd(const char *path) {
    size_t size=strlen(path)+1;
    assert(size<=sizeof(cwd_fs.cwd));
    memcpy(cwd_fs.cwd,path,size);
    cwd_task.fs=&cwd_fs;
    cwd_task.state=NAS_TASK_RUNNING;
}
int main(void) {
    assert(tmp_mkdir("/tmp/tree",0755)==0);
    assert(tmp_mkdir("/tmp/tree/sub",0755)==0);
    assert(tmp_mkdir("/tmp/empty",0755)==0);
    struct nas_tmp_file *dir=tmp_find("/tmp/tree");
    assert(dir && dir->is_dir && dir->nlink==1);
    assert(tmp_link("/tmp/tree","/tmp/linkdir")==-EPERM);
    assert(tmp_link("/ro","/tmp/x")==-EXDEV);
    assert(tmp_link("/tmp/tree","/ro")==-EXDEV);

    assert(tmp_parent_check("/tmp/tree/file")==0);
    struct nas_tmp_file *file=tmp_create("/tmp/tree/file",false,0644);
    assert(file && file->nlink==1);
    const uint64_t ino=tmp_number(file);
    static const uint8_t content[]="inode-data";
    uint8_t got[sizeof(content)]={0};
    assert(tmp_write_at(file,0,content,sizeof(content))==(int64_t)sizeof(content));
    assert(tmp_link("/tmp/tree/file","/tmp/alias")==0);
    assert(tmp_find("/tmp/alias")==file && file->nlink==2);
    assert(tmp_number(tmp_find("/tmp/alias"))==ino);
    assert(tmp_link("/tmp/tree/file","/tmp/alias")==-EEXIST);
    assert(tmp_rename("/tmp/tree/file","/tmp/alias",0)==0);
    assert(file->nlink==2); /* same inode: no-op */
    assert(tmp_unlink("/tmp/tree",0x200ULL)==-ENOTEMPTY);

    /* Rename updates all names in the moved subtree but never external
     * hardlinks, and updates cwd independently of inode pointer/offset. */
    putcwd("/tmp/tree/sub");
    assert(tmp_rename("/tmp/tree","/tmp/moved",0)==0);
    assert(!tmp_find("/tmp/tree/file"));
    assert(tmp_find("/tmp/moved/file")==file);
    assert(tmp_find("/tmp/alias")==file);
    assert(!strcmp(cwd_fs.cwd,"/tmp/moved/sub"));
    assert(!strcmp(tmp_name(dir),"/tmp/moved"));
    const struct nas_tmp_dentry *child=tmp_child_entry("/tmp/moved",0);
    assert(child && child->inode && child->path[0]=='/');
    assert(tmp_child_entry("/tmp/moved",32)==0);

    /* Files survive unlink and even final nlink==0 until all aliased open
     * file-descriptions are closed; backing data and inode are not copied. */
    nas_open_files[7].refcount=2;
    tmp_bind_slot(7,file);
    assert(tmp_unlink("/tmp/alias",0)==0 && file->nlink==1);
    assert(tmp_unlink("/tmp/moved/file",0)==0 && file->nlink==0);
    assert(!tmp_find("/tmp/alias"));
    assert(tmp_by_slot(7)==file && file->used);
    assert(tmp_read_at(file,0,got,sizeof(got))==(int64_t)sizeof(got));
    assert(!memcmp(got,content,sizeof(got)));
    nas_open_files[7].refcount=0; /* after two separate descriptor closes */
    unsigned freed_before=frame_freed;
    tmp_release_slot(7);
    assert(!file->used && frame_freed>freed_before);
    assert(tmp_by_slot(7)==0);

    /* Hard links to symlinks share the literal symlink inode by default. */
    assert(tmp_symlink("../source","/tmp/moved/sym")==0);
    struct nas_tmp_file *sym=tmp_find("/tmp/moved/sym");
    assert(sym && tmp_is_link(sym) && sym->nlink==1);
    assert(tmp_link("/tmp/moved/sym","/tmp/moved/sym2")==0);
    assert(tmp_find("/tmp/moved/sym2")==sym && sym->nlink==2);
    char target[256];
    assert(tmp_link_target(sym,target)==0 && !strcmp(target,"../source"));
    assert(tmp_unlink("/tmp/moved/sym",0)==0 && sym->nlink==1);
    assert(tmp_find("/tmp/moved/sym2")==sym);
    assert(tmp_unlink("/tmp/moved/sym2",0)==0 && !sym->used);

    assert(tmp_unlink("/tmp/moved",0x200ULL)==-ENOTEMPTY);
    /* A cwd still referencing the live nested directory prevents rmdir. */
    assert(tmp_unlink("/tmp/moved/sub",0x200ULL)==-EBUSY);
    putcwd("/tmp");
    assert(tmp_unlink("/tmp/moved/sub",0x200ULL)==0);
    assert(tmp_unlink("/tmp/moved",0x200ULL)==0);
    assert(!tmp_find("/tmp/moved"));
    /* A cwd or another generic VFS owner pins a tmpfs inode even when no
     * File slot is open. Last-link rmdir must NOT recycle its memory. */
    assert(tmp_mkdir("/tmp/owned-orphan",0700)==0);
    struct nas_tmp_file *orphan=tmp_find("/tmp/owned-orphan");
    assert(orphan && orphan->used);
    borrowed_vfs_pin=orphan;
    assert(tmp_unlink("/tmp/owned-orphan",0x200ULL)==0);
    assert(orphan->used && orphan->nlink==0 && nas_vfs_inode_is_pinned(orphan));
    borrowed_vfs_pin=0;
    tmp_inode_discard(orphan); /* precisely the final generic unpin hook */
    assert(!orphan->used);
    assert(frame_allocated>=frame_freed);
    puts("[M63 TMP PASS] dentry/inode, real hardlinks, rename, cwd, unlink/open, symlink");
    return 0;
}
