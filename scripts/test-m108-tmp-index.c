/* M108: exercise the real tmpfs namespace under realistic large source
 * directory loads. No fake dentry algorithm: link the same tmpfs.c and
 * tmp_namespace.c objects used by all three native kernel builds.
 *
 * Directory cookies in NaSeed's VFS are ordinal: insertions append,
 * removal compacts, and a rename within one directory preserves position.
 * Interleaved readers, mutations and inode slab recycling must not return
 * a previous incarnation of an inode or stale basename. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/tmpfs.h"
#include "../kernel/bootfs.h"
#include "../kernel/errno.h"

#define FRAMES 512U
#define FILES 1500U
#define RECYCLE 1200U
#define ROUNDS 3U

static _Alignas(4096) uint8_t frames[FRAMES][4096];
static bool busy[FRAMES];
static unsigned frame_allocs, frame_frees;
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<FRAMES;i++) {
        if(busy[i])continue;
        busy[i]=true;
        frame_allocs++;
        memset(frames[i],0,4096);
        return (uint64_t)(uintptr_t)frames[i];
    }
    return 0;
}
bool nas_frame_put(uint64_t phys) {
    for(unsigned i=0;i<FRAMES;i++) {
        if(phys!=(uint64_t)(uintptr_t)frames[i] || !busy[i])continue;
        busy[i]=false;
        frame_frees++;
        return true;
    }
    return false;
}
uint64_t nas_hhdm_offset(void) {return 0;}
static uint64_t fake_tick;
uint64_t nas_arch_monotonic_ns(void) {return ++fake_tick;}
bool nas_time_realtime_ns(uint64_t *now) {(void)now;return false;}

static struct bootfs_entry root_tmp={.path="/tmp",.mode=0040755U,.ino=1};
bool bootfs_mounted(void) {return true;}
const struct bootfs_entry *bootfs_find(const char *path) {
    return !strcmp(path,"/tmp")?&root_tmp:NULL;
}
bool bootfs_is_dir(const struct bootfs_entry *entry) {
    return entry && (entry->mode&0170000U)==0040000U;
}
static const void *pinned_inode;
bool nas_vfs_inode_is_pinned(const void *inode) {
    return inode && inode==pinned_inode;
}

static void item_name(unsigned index,char out[16]) {
    assert(index<10000U);
    snprintf(out,16,"item%04u",index);
}
static void recycle_name(unsigned index,char out[16]) {
    assert(index<10000U);
    snprintf(out,16,"cycle%04u",index);
}
static unsigned used_frames(void) {
    unsigned count=0;
    for(unsigned i=0;i<FRAMES;i++)if(busy[i])count++;
    return count;
}
static void expect_name(const void *parent,unsigned cursor,const char *name) {
    const struct nas_tmp_dentry *entry=tmp_child_entry_inode(parent,cursor);
    assert(entry && entry->used && entry->inode &&
           entry->parent_inode==parent && !strcmp(entry->name,name));
}

static void test_same_size_truncate_timestamps(void) {
    const void *root=&root_tmp;
    struct nas_tmp_file *lock=tmp_create_at(root,"ninja-lock",false,0600);
    assert(lock && lock->size==0);
    uint64_t previous=lock->mtime_ns;
    /* Linux fopen("wb") on an already-empty file still changes mtime.
     * Ninja 1.13.2 records this timestamp before each build edge, even
     * if no data bytes are subsequently written to the lock file. */
    for(unsigned i=0;i<3;i++) {
        assert(tmp_resize(lock,0)==0);
        assert(lock->mtime_ns>previous);
        assert(lock->ctime_ns==lock->mtime_ns);
        previous=lock->mtime_ns;
    }
    uint8_t byte=0x2e;
    assert(tmp_write_at(lock,0,&byte,1)==1);
    previous=lock->mtime_ns;
    assert(tmp_resize(lock,1)==0);
    assert(lock->mtime_ns>previous);
    assert(lock->ctime_ns==lock->mtime_ns);
    assert(tmp_unlink_at(root,"ninja-lock",0)==0);
    puts("[A015 NASEED TMPFS TRUNCATE PASS] same-size zero and nonzero sizes refresh mtime");
}
static void test_real_inode_chown(void) {
    const void *root=&root_tmp;
    struct nas_tmp_file *file=tmp_create_at(root,"owner-check",false,06755);
    assert(file && file->uid==0 && file->gid==0);
    uint64_t before=file->ctime_ns;
    assert(tmp_chown(file,42,84)==0);
    assert(file->uid==42 && file->gid==84 &&
           (file->mode&07777U)==0755U && file->ctime_ns>before);
    before=file->ctime_ns;
    assert(tmp_chown(file,UINT32_MAX,99)==0);
    assert(file->uid==42 && file->gid==99 && file->ctime_ns>before);
    assert(tmp_chown(file,UINT32_MAX,UINT32_MAX)==0);
    assert(file->uid==42 && file->gid==99);
    assert(tmp_unlink_at(root,"owner-check",0)==0);
    puts("[M80 TMPFS OWNER PASS] real uid/gid, -1 sentinel, ctime, set-ID reset");
}

/* The production namespace is exercised, including its borrowed bootfs
 * /tmp root. GNU Make's directory prerequisites need an actual directory
 * mtime change after create/delete/rename, not just child inode ctime. */
static void test_directory_mutation_timestamps(void) {
    const void *root=&root_tmp;
    uint64_t root_m=0,root_c=0;
    tmp_root_times(&root_m,&root_c);
    assert(root_m>0 && root_c>0);
    uint64_t stable_m=0,stable_c=0;
    tmp_root_times(&stable_m,&stable_c);
    assert(stable_m==root_m && stable_c==root_c);

    assert(tmp_mkdir_at(root,"meta-a",0755)==0);
    tmp_root_times(&stable_m,&stable_c);
    assert(stable_m>root_m && stable_c>root_c);
    root_m=stable_m;root_c=stable_c;
    assert(tmp_mkdir_at(root,"meta-b",0755)==0);
    tmp_root_times(&stable_m,&stable_c);
    assert(stable_m>root_m && stable_c>root_c);
    struct nas_tmp_file *a=tmp_find_child(root,"meta-a");
    struct nas_tmp_file *b=tmp_find_child(root,"meta-b");
    assert(a && b && a!=b);

    uint64_t am=a->mtime_ns,ac=a->ctime_ns;
    struct nas_tmp_file *file=tmp_create_at(a,"source",false,0600);
    assert(file && a->mtime_ns>am && a->ctime_ns>ac);
    am=a->mtime_ns;ac=a->ctime_ns;
    assert(!tmp_create_at(a,"source",false,0600));
    assert(a->mtime_ns==am && a->ctime_ns==ac);
    assert(tmp_symlink_at(a,"symlink","source")==0);
    assert(a->mtime_ns>am && a->ctime_ns>ac);

    uint64_t bm=b->mtime_ns,bc=b->ctime_ns;
    uint64_t file_ctime=file->ctime_ns;
    assert(tmp_link_at(a,"source",b,"alias")==0);
    assert(b->mtime_ns>bm && b->ctime_ns>bc);
    assert(file->ctime_ns>file_ctime && file->nlink==2);
    bm=b->mtime_ns;bc=b->ctime_ns;
    am=a->mtime_ns;ac=a->ctime_ns;
    assert(tmp_rename_at(a,"source",b,"moved",0)==0);
    assert(a->mtime_ns>am && a->ctime_ns>ac);
    assert(b->mtime_ns>bm && b->ctime_ns>bc);
    assert(tmp_find_child(b,"moved")==file);

    bm=b->mtime_ns;bc=b->ctime_ns;
    assert(tmp_rename_at(b,"moved",b,"moved",0)==0);
    assert(b->mtime_ns==bm && b->ctime_ns==bc);
    assert(tmp_rename_at(b,"missing",b,"elsewhere",0)==-ENOENT);
    assert(b->mtime_ns==bm && b->ctime_ns==bc);
    assert(tmp_create_at(b,"victim",false,0600));
    bm=b->mtime_ns;bc=b->ctime_ns;
    assert(tmp_rename_at(b,"moved",b,"victim",0)==0);
    assert(b->mtime_ns>bm && b->ctime_ns>bc);
    assert(tmp_find_child(b,"victim")==file);
    bm=b->mtime_ns;bc=b->ctime_ns;
    assert(tmp_unlink_at(b,"victim",0)==0);
    assert(b->mtime_ns>bm && b->ctime_ns>bc);
    assert(tmp_unlink_at(b,"alias",0)==0);

    am=a->mtime_ns;ac=a->ctime_ns;
    assert(tmp_unlink_at(a,"symlink",0)==0);
    assert(a->mtime_ns>am && a->ctime_ns>ac);
    root_m=stable_m;root_c=stable_c;
    assert(tmp_unlink_at(root,"meta-a",0x200ULL)==0);
    assert(tmp_unlink_at(root,"meta-b",0x200ULL)==0);
    tmp_root_times(&stable_m,&stable_c);
    assert(stable_m>root_m && stable_c>root_c);
    puts("[M82 TMPFS DIRECTORY TIME PASS] original inode root, create/link/symlink/rename/unlink, failed and no-op mutations");
}

static void test_large_tree(void) {
    const void *root=&root_tmp;
    assert(tmp_mkdir_at(root,"big",0755)==0);
    assert(tmp_mkdir_at(root,"dest",0755)==0);
    struct nas_tmp_file *big=tmp_find_child(root,"big");
    struct nas_tmp_file *dest=tmp_find_child(root,"dest");
    assert(big && dest && big!=dest);
    assert(tmp_mkdir_at(big,"sub",0755)==0);
    struct nas_tmp_file *sub=tmp_find_child(big,"sub");
    assert(sub && tmp_mkdir_at(sub,"deep",0755)==0);
    struct nas_tmp_file *deep=tmp_find_child(sub,"deep");
    assert(deep && tmp_create_at(deep,"leaf",false,0600));
    assert(tmp_create_at(dest,"marker",false,0600));

    struct nas_tmp_file *first=NULL;
    for(unsigned i=0;i<FILES;i++) {
        char name[16];item_name(i,name);
        struct nas_tmp_file *created=tmp_create_at(big,name,false,0644);
        assert(created && created->used && created->nlink==1);
        if(!i)first=created;
    }
    /* Real CMake/Ninja source copies create thousands of siblings in /tmp.
     * Every ordinal is checked, including after other readers seek around. */
    expect_name(big,0,"sub");
    for(unsigned i=0;i<FILES;i++) {
        char name[16];item_name(i,name);
        expect_name(big,i+1,name);
        if((i&31U)==0U)expect_name(dest,0,"marker");
    }
    assert(tmp_child_entry_inode(big,FILES+1U)==NULL);
    assert(tmp_child_entry_inode(dest,1U)==NULL);
    for(unsigned i=0;i<FILES;i++) {
        unsigned ordinal=(i*541U)%FILES;
        char name[16];item_name(ordinal,name);
        expect_name(big,ordinal+1,name);
        assert(tmp_find_child(big,name));
    }

    /* A directory rename that would overrun one deep descendant must
     * reject atomically, leaving every other sibling visible at old keys. */
    char overlong[239];
    memset(overlong,'x',sizeof(overlong)-1);
    overlong[sizeof(overlong)-1]=0;
    assert(tmp_rename_at(root,"big",root,overlong,0)==-ENAMETOOLONG);
    assert(tmp_find_child(root,"big")==big);
    assert(tmp_find_child(root,overlong)==NULL);
    assert(tmp_find_child(big,"item0000")==first);
    assert(tmp_find_child(sub,"deep")==deep);
    assert(tmp_find_child(deep,"leaf"));

    /* A cross-directory move must invalidate both independent ordinal
     * states while not touching unrelated directory cursors. */
    assert(tmp_rename_at(big,"item0001",dest,"moved",0)==0);
    assert(tmp_find_child(big,"item0001")==NULL);
    assert(tmp_find_child(dest,"moved"));
    expect_name(dest,0,"marker");
    expect_name(dest,1,"moved");
    expect_name(big,0,"sub");
    expect_name(big,1,"item0000");
    expect_name(big,2,"item0002");

    /* Same-parent rename-overwrite must detach only the target alias and
     * keep the source at its original ordinal. A pinned displaced inode
     * must survive until its actual final VFS owner drops it. */
    struct nas_tmp_file *replacement=tmp_create_at(dest,"replacement",false,0600);
    struct nas_tmp_file *victim=tmp_create_at(dest,"victim",false,0600);
    assert(replacement && victim && replacement!=victim);
    const uint64_t victim_ino=tmp_number(victim);
    pinned_inode=victim;
    assert(tmp_rename_at(dest,"replacement",dest,"victim",0)==0);
    assert(tmp_find_child(dest,"replacement")==NULL);
    assert(tmp_find_child(dest,"victim")==replacement);
    assert(victim->used && victim->nlink==0);
    expect_name(dest,2,"victim");
    assert(tmp_child_entry_inode(dest,3)==NULL);
    pinned_inode=NULL;
    tmp_inode_discard(victim);
    assert(!victim->used);
    struct nas_tmp_file *reborn=tmp_create_at(dest,"reborn",false,0600);
    assert(reborn && tmp_number(reborn)!=victim_ino);
    assert(tmp_find_child(dest,"reborn")==reborn);
    expect_name(dest,3,"reborn");

    /* Delete about one third of the entries and ensure removed names
     * never appear through a previous ordinal-cursor cache. */
    for(unsigned i=0;i<FILES;i++) {
        if(i==1 || (i%3U)!=0U)continue;
        char name[16];item_name(i,name);
        assert(tmp_unlink_at(big,name,0)==0);
        assert(tmp_find_child(big,name)==NULL);
    }
    unsigned expected=1;
    expect_name(big,0,"sub");
    for(unsigned i=0;i<FILES;i++) {
        if(i==1 || i%3U==0U)continue;
        char name[16];item_name(i,name);
        expect_name(big,expected++,name);
    }
    assert(tmp_child_entry_inode(big,expected)==NULL);

    /* Hard-linked inode survives both names while an independent VFS
     * open-description pins it, then can be recycled after final unpin. */
    struct nas_tmp_file *hard=tmp_find_child(big,"item0002");
    assert(hard && tmp_link_at(big,"item0002",dest,"hard")==0);
    assert(tmp_find_child(dest,"hard")==hard && hard->nlink==2);
    pinned_inode=hard;
    assert(tmp_unlink_at(big,"item0002",0)==0);
    assert(tmp_unlink_at(dest,"hard",0)==0);
    assert(hard->used && hard->nlink==0);
    pinned_inode=NULL;
    tmp_inode_discard(hard);
    assert(!hard->used);
    assert(tmp_find_child(big,"item0002")==NULL);
    assert(tmp_find_child(dest,"hard")==NULL);

    /* Move a directory after successful descendant-path preflight, then
     * check its parent identity, contents, and both directory listings. */
    assert(tmp_rename_at(big,"sub",dest,"sub",0)==0);
    assert(tmp_find_child(big,"sub")==NULL);
    assert(tmp_find_child(dest,"sub")==sub);
    assert(tmp_dir_parent_inode(sub)==dest);
    assert(tmp_find_child(sub,"deep")==deep);
    assert(tmp_find_child(deep,"leaf"));

    /* A name introduced after removals must append, never expose an
     * old tombstone under a new ordinal or a stale inode alias. */
    assert(tmp_create_at(big,"new-after-delete",false,0644));
    unsigned count=0;
    while(tmp_child_entry_inode(big,count))count++;
    assert(count>0 && count<FILES);
    expect_name(big,count-1,"new-after-delete");

    for(unsigned i=0;i<FILES;i++) {
        char name[16];item_name(i,name);
        if(tmp_find_child(big,name))assert(tmp_unlink_at(big,name,0)==0);
    }
    assert(tmp_unlink_at(big,"new-after-delete",0)==0);
    assert(tmp_unlink_at(root,"big",0x200ULL)==0);
    assert(tmp_unlink_at(dest,"moved",0)==0);
    assert(tmp_unlink_at(dest,"marker",0)==0);
    assert(tmp_unlink_at(dest,"victim",0)==0);
    assert(tmp_unlink_at(dest,"reborn",0)==0);
    assert(tmp_unlink_at(deep,"leaf",0)==0);
    assert(tmp_unlink_at(sub,"deep",0x200ULL)==0);
    assert(tmp_unlink_at(dest,"sub",0x200ULL)==0);
    assert(tmp_unlink_at(root,"dest",0x200ULL)==0);
    assert(tmp_child_entry_inode(root,0)==NULL);
    puts("[M108 TMP LARGE TREE PASS] 1500 siblings, ordinal caches, rename transaction and unlink/rebind");
}

static void test_inode_recycling(void) {
    const void *root=&root_tmp;
    assert(tmp_mkdir_at(root,"hot",0755)==0);
    struct nas_tmp_file *hot=tmp_find_child(root,"hot");
    assert(hot);
    unsigned steady_frames=0;
    for(unsigned round=0;round<ROUNDS;round++) {
        for(unsigned i=0;i<RECYCLE;i++) {
            char name[16];recycle_name(i,name);
            assert(tmp_create_at(hot,name,false,0600));
        }
        for(unsigned i=0;i<RECYCLE;i++) {
            char name[16];recycle_name(i,name);
            assert(tmp_find_child(hot,name));
            expect_name(hot,i,name);
        }
        for(unsigned i=0;i<RECYCLE;i++) {
            char name[16];recycle_name(i,name);
            assert(tmp_unlink_at(hot,name,0)==0);
            assert(tmp_find_child(hot,name)==NULL);
        }
        assert(tmp_child_entry_inode(hot,0)==NULL);
        unsigned occupied=used_frames();
        if(round==0)steady_frames=occupied;
        else assert(occupied<=steady_frames);
    }
    assert(tmp_unlink_at(root,"hot",0x200ULL)==0);
    assert(frame_allocs>=frame_frees && used_frames()<FRAMES);
    puts("[M108 TMP RECYCLE PASS] 3 x 1200 real create/readdir/unlink, stable slab footprint");
}

int main(void) {
    test_same_size_truncate_timestamps();
    test_real_inode_chown();
    test_directory_mutation_timestamps();
    test_large_tree();
    test_inode_recycling();
    puts("[M108 TMP INDEX PASS]");
    return 0;
}
