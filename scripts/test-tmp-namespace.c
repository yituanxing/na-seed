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
/* M63 links the actual tmpfs data/namespace objects without VFS to keep
 * its namespace test isolated. Model only the VFS owner signal here; the
 * separate generic VFS fixture tests the REAL refcounted lease table. */
static const void *borrowed_vfs_pin;
bool nas_vfs_inode_is_pinned(const void *identity) {
    return identity && identity==borrowed_vfs_pin;
}
int main(void) {
    const void *root=&root_tmp;
    assert(tmp_mkdir_at(root,"tree",0755)==0);
    struct nas_tmp_file *dir=tmp_find_child(root,"tree");
    assert(dir);
    assert(tmp_mkdir_at(dir,"sub",0755)==0);
    struct nas_tmp_file *sub=tmp_find_child(dir,"sub");
    assert(sub);
    assert(tmp_mkdir_at(root,"empty",0755)==0);
    assert(dir && dir->is_dir && dir->nlink==1);
    assert(tmp_link_at(root,"tree",root,"linkdir")==-EPERM);
    assert(tmp_link_at(0,"ro",root,"x")==-ENOENT);
    assert(tmp_link_at(root,"tree",0,"ro")==-ENOENT);

    struct nas_tmp_file *file=tmp_create_at(dir,"file",false,0644);
    assert(file && file->nlink==1);
    const uint64_t ino=tmp_number(file);
    static const uint8_t content[]="inode-data";
    uint8_t got[sizeof(content)]={0};
    assert(tmp_write_at(file,0,content,sizeof(content))==(int64_t)sizeof(content));
    assert(tmp_link_at(dir,"file",root,"alias")==0);
    assert(tmp_find_child(root,"alias")==file && file->nlink==2);
    assert(tmp_number(tmp_find_child(root,"alias"))==ino);
    assert(tmp_link_at(dir,"file",root,"alias")==-EEXIST);
    assert(tmp_rename_at(dir,"file",root,"alias",0)==0);
    assert(file->nlink==2); /* same inode: no-op */
    assert(tmp_unlink_at(root,"tree",0x200ULL)==-ENOTEMPTY);

    /* Rename changes just the shared directory edge. Descendants and
     * hardlinks resolve through inode ownership; current working directory
     * string revalidation belongs to the independent real-VFS Path tests. */
    assert(tmp_rename_at(root,"tree",root,"moved",0)==0);
    assert(!tmp_find_child(root,"tree"));
    assert(tmp_find_child(root,"moved")==dir);
    assert(tmp_find_child(dir,"file")==file);
    assert(tmp_find_child(root,"alias")==file);
    assert(tmp_find_child(dir,"sub")==sub);
    const struct nas_tmp_dentry *child=tmp_child_entry_inode(dir,0);
    assert(child && child->inode && !strcmp(child->name,"sub"));
    assert(child->parent_inode==dir);
    assert(tmp_dir_parent_inode(dir)==(const void *)&root_tmp);
    assert(tmp_child_entry_inode(dir,0)==child);
    assert(tmp_find_child(dir,"sub")==sub);
    assert(tmp_rename_at(root,"moved",sub,"loop",0)==-EINVAL);
    assert(tmp_mkdir_at(dir,"inode-created",0700)==0);
    assert(tmp_find_child(dir,"inode-created")!=NULL);
    assert(tmp_rename_at(dir,"inode-created",dir,"inode-moved",0)==0);
    assert(tmp_find_child(dir,"inode-moved")!=NULL);
    assert(tmp_unlink_at(dir,"inode-moved",0x200ULL)==0);
    /* AF_UNIX nodes are published with S_IFSOCK already set: never expose
     * the old regular-file-then-retag two-phase representation.  The VFS
     * mknod owner handles errno and mount policy; tmpfs owns atomic inode
     * creation and dentry publication. */
    assert(tmp_mknod_at(dir,"sock",0100600U)==NULL);
    struct nas_tmp_file *sock=tmp_mknod_at(dir,"sock",0140600U);
    assert(sock && sock->used && sock->nlink==1 && !sock->is_dir);
    assert((sock->mode&0170000U)==0140000U && (sock->mode&07777U)==0600U);
    assert(tmp_find_child(dir,"sock")==sock);
    assert(tmp_mknod_at(dir,"sock",0140600U)==NULL);
    assert(tmp_rename_at(dir,"sock",dir,"sock-renamed",0)==0);
    assert(!tmp_find_child(dir,"sock") && tmp_find_child(dir,"sock-renamed")==sock);
    assert(tmp_unlink_at(dir,"sock-renamed",0)==0);
    assert(!sock->used);
    assert(tmp_child_entry_inode(dir,32)==0);

    /* Files survive unlink and even final nlink==0 until all aliased open
     * file-descriptions are closed; backing data and inode are not copied. */
    borrowed_vfs_pin=file; /* stand-in for real VFS inode pin */
    assert(tmp_unlink_at(root,"alias",0)==0 && file->nlink==1);
    assert(tmp_unlink_at(dir,"file",0)==0 && file->nlink==0);
    assert(!tmp_find_child(root,"alias"));
    assert(nas_vfs_inode_is_pinned(file) && file->used);
    assert(tmp_read_at(file,0,got,sizeof(got))==(int64_t)sizeof(got));
    assert(!memcmp(got,content,sizeof(got)));
    borrowed_vfs_pin=NULL; /* VFS final File description closed */
    unsigned freed_before=frame_freed;
    tmp_inode_discard(file); /* VFS last_inode_unpin callback */
    assert(!file->used && frame_freed>freed_before);

    /* Hard links to symlinks share the literal symlink inode by default. */
    assert(tmp_symlink_at(dir,"sym","../source")==0);
    struct nas_tmp_file *sym=tmp_find_child(dir,"sym");
    assert(sym && tmp_is_link(sym) && sym->nlink==1);
    assert(tmp_link_at(dir,"sym",dir,"sym2")==0);
    assert(tmp_find_child(dir,"sym2")==sym && sym->nlink==2);
    char target[256];
    assert(tmp_link_target(sym,target)==0 && !strcmp(target,"../source"));
    assert(tmp_unlink_at(dir,"sym",0)==0 && sym->nlink==1);
    assert(tmp_find_child(dir,"sym2")==sym);
    assert(tmp_unlink_at(dir,"sym2",0)==0 && !sym->used);

    assert(tmp_unlink_at(root,"moved",0x200ULL)==-ENOTEMPTY);
    /* Linux allows rmdir(cwd), even while cwd owns its inode. Pathname
     * disappearance is NOT inode destruction. A historical task->fs->cwd
     * string must never veto namespace removal with EBUSY. This isolated
     * tmpfs fixture models the generic VFS owner's pin; the independent
     * real-VFS fixture verifies owned-Path detached behavior, and native
     * ring3 probes verify getcwd()==ENOENT after rmdir(cwd). */
    struct nas_tmp_file *cwd_orphan=tmp_find_child(root,"empty");
    assert(cwd_orphan && cwd_orphan->is_dir && cwd_orphan->nlink==1);
    borrowed_vfs_pin=cwd_orphan;
    assert(tmp_unlink_at(root,"empty",0x200ULL)==0);
    assert(!tmp_find_child(root,"empty") && cwd_orphan->used &&
           cwd_orphan->nlink==0 && nas_vfs_inode_is_pinned(cwd_orphan));
    assert(tmp_find_child(cwd_orphan,"new")==NULL);
    assert(tmp_create_at(cwd_orphan,"new",false,0600)==NULL);
    assert(tmp_mkdir_at(cwd_orphan,"newdir",0700)==-ENOENT);
    assert(tmp_symlink_at(cwd_orphan,"newlink","target")==-ENOENT);
    assert(tmp_find_child(cwd_orphan,"new")==NULL);
    borrowed_vfs_pin=NULL;
    tmp_inode_discard(cwd_orphan); /* last owned cwd Path released */
    assert(!cwd_orphan->used);
    /* Namespace rename remains independent of process/task lifetime.
     * The FS backend no longer inspects Task state or cwd text caches. */
    assert(tmp_rename_at(root,"moved",root,"after-exit",0)==0);
    assert(tmp_find_child(dir,"sub")==sub);
    assert(tmp_rename_at(root,"after-exit",root,"moved",0)==0);
    assert(tmp_mkdir_at(root,"after-exit-empty",0700)==0);
    assert(tmp_unlink_at(root,"after-exit-empty",0x200ULL)==0);
    assert(tmp_unlink_at(dir,"sub",0x200ULL)==0);
    assert(tmp_unlink_at(root,"moved",0x200ULL)==0);
    assert(!tmp_find_child(root,"moved"));
    /* A cwd or another generic VFS owner pins a tmpfs inode even when no
     * File slot is open. Last-link rmdir must NOT recycle its memory. */
    assert(tmp_mkdir_at(root,"owned-orphan",0700)==0);
    struct nas_tmp_file *orphan=tmp_find_child(root,"owned-orphan");
    assert(orphan && orphan->used);
    borrowed_vfs_pin=orphan;
    assert(tmp_unlink_at(root,"owned-orphan",0x200ULL)==0);
    assert(orphan->used && orphan->nlink==0 && nas_vfs_inode_is_pinned(orphan));
    borrowed_vfs_pin=0;
    tmp_inode_discard(orphan); /* precisely the final generic unpin hook */
    assert(!orphan->used);
    assert(frame_allocated>=frame_freed);
    puts("[M63 TMP PASS] dentry/inode, real hardlinks, rename, cwd, unlink/open, symlink");
    return 0;
}
