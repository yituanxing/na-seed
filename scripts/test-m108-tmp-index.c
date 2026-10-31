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
/* The production tmpfs keeps one frame owner while each MAP_SHARED PTE
 * independently pins the SAME frame. Model reference counts, not a boolean
 * allocation bitmap, so truncate/munmap lifetime mistakes fail here. */
static unsigned frame_refs[FRAMES];
static unsigned frame_allocs, frame_frees;
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<FRAMES;i++) {
        if(frame_refs[i])continue;
        frame_refs[i]=1;
        frame_allocs++;
        memset(frames[i],0,4096);
        return (uint64_t)(uintptr_t)frames[i];
    }
    return 0;
}
bool nas_frame_get(uint64_t phys) {
    for(unsigned i=0;i<FRAMES;i++) {
        if(phys!=(uint64_t)(uintptr_t)frames[i] || !frame_refs[i] ||
           frame_refs[i]==UINT32_MAX)continue;
        frame_refs[i]++;
        return true;
    }
    return false;
}
bool nas_frame_put(uint64_t phys) {
    for(unsigned i=0;i<FRAMES;i++) {
        if(phys!=(uint64_t)(uintptr_t)frames[i] || !frame_refs[i])continue;
        if(!--frame_refs[i])frame_frees++;
        return true;
    }
    return false;
}
uint32_t nas_frame_refs(uint64_t phys) {
    for(unsigned i=0;i<FRAMES;i++)
        if(phys==(uint64_t)(uintptr_t)frames[i])return frame_refs[i];
    return 0;
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
    for(unsigned i=0;i<FRAMES;i++)if(frame_refs[i])count++;
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

    /* Path walking now owns Linux PATH_MAX independently of its 256-byte
     * diagnostic projection. A legal long COMPONENT must remain valid even
     * when a descendant exceeds that former whole-path buffer; inode and
     * child identities must survive the long-name round trip. */
    char overlong[239];
    memset(overlong,'x',sizeof(overlong)-1);
    overlong[sizeof(overlong)-1]=0;
    assert(tmp_rename_at(root,"big",root,overlong,0)==0);
    assert(tmp_find_child(root,"big")==NULL);
    assert(tmp_find_child(root,overlong)==big);
    assert(tmp_find_child(sub,"deep")==deep);
    assert(tmp_find_child(deep,"leaf"));
    assert(tmp_rename_at(root,overlong,root,"big",0)==0);
    assert(tmp_find_child(root,"big")==big);
    char invalid_leaf[257];
    memset(invalid_leaf,'q',sizeof(invalid_leaf)-1);
    invalid_leaf[sizeof(invalid_leaf)-1]=0;
    assert(tmp_rename_at(root,"big",root,invalid_leaf,0)==-EINVAL);
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

/* MAP_SHARED reuses the production tmpfs data frame, not a fresh snapshot.
 * This is deliberately below the VFS/MM integration test: it proves the
 * provider's independent inode/page owners, sparse I/O aliasing and ref
 * lifetime when a file is shrunk and unlinked with a mapping still alive. */
static void test_shared_page_ownership(void) {
    const void *root=&root_tmp;
    unsigned before=used_frames();
    struct nas_tmp_file *file=tmp_create_at(root,"shared-frame",false,0600);
    assert(file);
    uint8_t contents[2]={0x31,0x52};
    assert(tmp_write_at(file,0,&contents[0],1)==1);
    assert(tmp_write_at(file,TMP_PAGE_SIZE,&contents[1],1)==1);
    uint64_t first=0,alias=0,second=0;
    assert(tmp_acquire_shared_page(file,0,&first)==0);
    assert(tmp_acquire_shared_page(file,0,&alias)==0);
    assert(first && first==alias && nas_frame_refs(first)==3);
    assert(tmp_acquire_shared_page(file,TMP_PAGE_SIZE,&second)==0);
    assert(second && second!=first && nas_frame_refs(second)==2);

    uint8_t *mapped=(uint8_t *)(uintptr_t)first;
    mapped[0]=0x7b;
    uint8_t observed=0;
    assert(tmp_read_at(file,0,&observed,1)==1 && observed==0x7b);
    observed=0x4c;
    assert(tmp_write_at(file,0,&observed,1)==1 && mapped[0]==0x4c);
    /* An unlinked inode remains live while its independent VMA lease is
     * held. Its data frame survives the inode's own truncate-time put. */
    pinned_inode=file;
    assert(tmp_unlink_at(root,"shared-frame",0)==0);
    assert(file->used && !file->nlink);
    assert(tmp_resize(file,1)==0);
    assert(nas_frame_refs(second)==1);
    uint64_t absent=0;
    assert(tmp_acquire_shared_page(file,TMP_PAGE_SIZE,&absent)==-EFAULT);
    assert(!absent);
    /* Regrowth must reacquire the current inode page, never the stale
     * old mapping's detached frame. Actual PTE invalidation belongs to MM. */
    assert(tmp_resize(file,2*TMP_PAGE_SIZE)==0);
    uint64_t renewed=0;
    assert(tmp_acquire_shared_page(file,TMP_PAGE_SIZE,&renewed)==0);
    assert(renewed && renewed!=second);
    assert(*(const uint8_t *)(uintptr_t)renewed==0);
    assert(nas_frame_put(renewed));
    assert(nas_frame_put(second));
    assert(nas_frame_put(alias) && nas_frame_put(first));
    pinned_inode=NULL;
    tmp_inode_discard(file);
    assert(used_frames()==before);
    /* Independent Linux oracle above proves both growth paths must zero
     * bytes beyond old EOF that a shared mapper previously dirtied. Pwrite
     * into a later sparse hole and ftruncate-grow share this exact producer
     * invariant rather than maintaining two subtly different algorithms. */
    struct nas_tmp_file *tail=tmp_create_at(root,"shared-tail",false,0600);
    assert(tail);
    uint8_t initial_bytes[100];
    memset(initial_bytes,'a',sizeof(initial_bytes));
    assert(tmp_write_at(tail,0,initial_bytes,sizeof(initial_bytes))==
           (int64_t)sizeof(initial_bytes));
    uint64_t tail_pa=0;
    assert(tmp_acquire_shared_page(tail,0,&tail_pa)==0);
    uint8_t *tail_mapping=(uint8_t *)(uintptr_t)tail_pa;
    tail_mapping[200]='X';
    uint8_t value='Z';
    assert(tmp_write_at(tail,300,&value,1)==1);
    value=1;
    assert(tmp_read_at(tail,200,&value,1)==1 && value==0);
    tail_mapping[350]='Y';
    assert(tmp_resize(tail,400)==0);
    value=1;
    assert(tmp_read_at(tail,350,&value,1)==1 && value==0);
    pinned_inode=tail;
    assert(tmp_unlink_at(root,"shared-tail",0)==0);
    assert(tail->used && !tail->nlink);
    assert(nas_frame_put(tail_pa));
    pinned_inode=NULL;
    tmp_inode_discard(tail);
    assert(used_frames()==before);
    puts("[A005 TMPFS SHARED FRAME PASS] genuine alias, unlink pin, truncate and sparse regrow ownership");
    puts("[A005 TMPFS EOF HOLE PASS] shared tail zeroed on pwrite-hole and ftruncate growth");
}

static void test_inode_xattr_and_fiemap(void) {
    const void *root=&root_tmp;
    unsigned before=used_frames();
    struct nas_tmp_file *file=tmp_create_at(root,"xattr-source",false,0600);
    assert(file);
    const uint8_t value[]={'a','l','p','i','n','e'};
    uint8_t buffer[32]={0};
    assert(tmp_getxattr(file,"user.batch",NULL,0)==-ENODATA);
    assert(tmp_setxattr(file,"security.capability",value,6,0)==-EOPNOTSUPP);
    assert(tmp_setxattr(file,"user.batch",value,6,2)==-ENODATA);
    assert(tmp_setxattr(file,"user.batch",value,6,1)==0);
    uint64_t allocated_xattr=file->xattr_frame;
    assert(allocated_xattr && nas_frame_refs(allocated_xattr)==1);
    assert(tmp_setxattr(file,"user.batch",value,6,1)==-EEXIST);
    assert(tmp_getxattr(file,"user.batch",NULL,0)==6);
    assert(tmp_getxattr(file,"user.batch",buffer,5)==-ERANGE);
    assert(tmp_getxattr(file,"user.batch",buffer,6)==6);
    assert(!memcmp(buffer,value,6));
    assert(tmp_setxattr(file,"user.extra",value,3,0)==0);
    assert(tmp_listxattr(file,NULL,0)==22);
    assert(tmp_listxattr(file,buffer,21)==-ERANGE);
    assert(tmp_listxattr(file,buffer,sizeof(buffer))==22);
    assert(!strcmp((char *)buffer,"user.batch"));
    assert(!strcmp((char *)buffer+11,"user.extra"));
    /* Inode attributes survive a hardlink and move, including after the
     * last visible link is removed but the File lease remains pinned. */
    assert(tmp_link_at(root,"xattr-source",root,"xattr-alias")==0);
    assert(tmp_rename_at(root,"xattr-source",root,"xattr-moved",0)==0);
    assert(tmp_find_child(root,"xattr-alias")==file);
    assert(tmp_find_child(root,"xattr-moved")==file);
    assert(tmp_getxattr(tmp_find_child(root,"xattr-moved"),"user.batch",NULL,0)==6);

    uint8_t byte='Q';
    assert(tmp_write_at(file,0,&byte,1)==1);
    assert(tmp_write_at(file,3*TMP_PAGE_SIZE,&byte,1)==1);
    uint64_t logical=0,length=0;
    bool last=true;
    assert(tmp_fiemap_next(file,0,file->size,&logical,&length,&last)==1);
    assert(logical==0 && length==TMP_PAGE_SIZE && !last);
    assert(tmp_fiemap_next(file,TMP_PAGE_SIZE,file->size,
                           &logical,&length,&last)==1);
    assert(logical==3*TMP_PAGE_SIZE && length==TMP_PAGE_SIZE && last);
    assert(tmp_fiemap_next(file,4*TMP_PAGE_SIZE,file->size,
                           &logical,&length,&last)==0);
    assert(tmp_resize(file,1)==0);
    assert(tmp_fiemap_next(file,0,file->size,&logical,&length,&last)==1);
    assert(logical==0 && length==TMP_PAGE_SIZE && last);
    assert(tmp_getxattr(file,"user.batch",NULL,0)==6);
    pinned_inode=file;
    assert(tmp_unlink_at(root,"xattr-alias",0)==0);
    assert(tmp_unlink_at(root,"xattr-moved",0)==0);
    assert(file->used && !file->nlink && nas_frame_refs(allocated_xattr)==1);
    assert(tmp_removexattr(file,"user.batch")==0);
    assert(tmp_getxattr(file,"user.batch",NULL,0)==-ENODATA);
    assert(tmp_removexattr(file,"user.batch")==-ENODATA);
    assert(tmp_listxattr(file,NULL,0)==11);
    pinned_inode=NULL;
    tmp_inode_discard(file);
    assert(!file->used && nas_frame_refs(allocated_xattr)==0);
    assert(used_frames()==before);
    puts("[M130 TMP XATTR/FIEMAP PASS] inode-owned metadata, hardlinks, orphan, sparse logical extents, frame reclaim");
}

static void test_sparse_fiemap_watermark(void) {
    const void *root=&root_tmp;
    unsigned before=used_frames();
    struct nas_tmp_file *file=tmp_create_at(root,"fiemap-many",false,0600);
    assert(file);
    uint64_t logical=0,length=0;
    bool last=false;
    /* Sparse ftruncate must not allocate a page or fabricate an extent. */
    assert(tmp_resize(file,1024*TMP_PAGE_SIZE)==0);
    assert(file->highest_allocated_data_page_plus_one==0);
    assert(tmp_fiemap_next(file,0,file->size,&logical,&length,&last)==0);

    uint8_t byte='S';
    for (uint64_t i=0;i<96;i++)
        assert(tmp_write_at(file,i*2*TMP_PAGE_SIZE,&byte,1)==1);
    assert(file->highest_allocated_data_page_plus_one==191);
    /* LAST must describe the whole inode, even when the query ends early. */
    assert(tmp_fiemap_next(file,0,TMP_PAGE_SIZE,&logical,&length,&last)==1);
    assert(logical==0 && length==TMP_PAGE_SIZE && !last);
    for (uint64_t i=0;i<96;i++) {
        assert(tmp_fiemap_next(file,i*2*TMP_PAGE_SIZE,file->size,
                               &logical,&length,&last)==1);
        assert(logical==i*2*TMP_PAGE_SIZE && length==TMP_PAGE_SIZE);
        assert(last==(i==95));
    }
    assert(tmp_fiemap_next(file,191*TMP_PAGE_SIZE,file->size,
                           &logical,&length,&last)==0);
    /* Retaining a partial EOF page and deleting later extents must update
     * the watermark. The following sparse regrowth must not change it. */
    assert(tmp_resize(file,179*TMP_PAGE_SIZE+1)==0);
    assert(file->highest_allocated_data_page_plus_one==179);
    assert(tmp_resize(file,1024*TMP_PAGE_SIZE)==0);
    assert(file->highest_allocated_data_page_plus_one==179);
    assert(tmp_fiemap_next(file,178*TMP_PAGE_SIZE,file->size,
                           &logical,&length,&last)==1);
    assert(logical==178*TMP_PAGE_SIZE && length==TMP_PAGE_SIZE && last);

    tmp_truncate(file);
    assert(file->highest_allocated_data_page_plus_one==0);
    assert(tmp_resize(file,12*TMP_PAGE_SIZE)==0);
    assert(tmp_fiemap_next(file,0,file->size,&logical,&length,&last)==0);
    /* MAP_SHARED page faults allocate file data without going through
     * write(2), and therefore must also update the shared allocation owner. */
    uint64_t shared=0;
    assert(tmp_acquire_shared_page(file,9*TMP_PAGE_SIZE,&shared)==0);
    assert(file->highest_allocated_data_page_plus_one==10);
    assert(tmp_fiemap_next(file,0,file->size,&logical,&length,&last)==1);
    assert(logical==9*TMP_PAGE_SIZE && length==TMP_PAGE_SIZE && last);
    assert(nas_frame_put(shared));
    assert(tmp_unlink_at(root,"fiemap-many",0)==0);
    assert(!file->used && file->highest_allocated_data_page_plus_one==0);
    struct nas_tmp_file *reused=tmp_create_at(root,"fiemap-reuse",false,0600);
    assert(reused==file && reused->highest_allocated_data_page_plus_one==0);
    assert(tmp_unlink_at(root,"fiemap-reuse",0)==0);
    assert(used_frames()==before);
    puts("[M132 TMP SPARSE FIEMAP PASS] 96 extents, bounded LAST, shrink/regrow, MAP_SHARED and inode reuse");
}

int main(void) {
    test_same_size_truncate_timestamps();
    test_real_inode_chown();
    test_directory_mutation_timestamps();
    test_large_tree();
    test_inode_recycling();
    test_shared_page_ownership();
    test_inode_xattr_and_fiemap();
    test_sparse_fiemap_watermark();
    puts("[M108 TMP INDEX PASS]");
    return 0;
}
