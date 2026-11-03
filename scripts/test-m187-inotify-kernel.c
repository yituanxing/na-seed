/* M187 real inotify provider with deterministic Linux wire ABI, not a
 * reimplemented queue. Only platform boundaries (FD, namei, heap) are fake. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fd.h"
#include "../kernel/task.h"
#include "../kernel/errno.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/fs/path_request.h"

static struct nas_task task;
static struct nas_files files;
struct nas_task *nas_current_task=&task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static unsigned path_drops,allocations;
static struct nas_fs_node path_identity;
void *nas_heap_calloc(size_t count,size_t size) {
    void *p=calloc(count,size);if(p)allocations++;return p;
}
bool nas_heap_free(void *pointer) {
    if(!pointer)return false;
    assert(allocations);allocations--;free(pointer);return true;
}
int nas_fd_reserve(const struct nas_task *owner,unsigned minimum,unsigned count,
                   unsigned fds[2],unsigned slots[2],const char *operation) {
    (void)minimum;(void)operation;assert(owner==&task && count==1);
    fds[0]=5;slots[0]=33;return 0;
}
bool nas_fd_install_new(struct nas_task *owner,unsigned fd,unsigned slot,
                        uint8_t kind,uint64_t status,bool cloexec) {
    assert(owner==&task && fd==5 && slot==33);
    owner->fds->entries[fd]=(struct nas_fd_entry){kind,(uint8_t)slot,cloexec};
    nas_open_files[slot].refcount=1;
    nas_open_files[slot].status_flags=status;
    return true;
}
int nas_fs_user_lookup_begin(uint64_t dirfd,uint64_t user_path,bool follow,
                             struct nas_fs_path_request *request,
                             struct nas_vfs_path *out,
                             struct nas_fs_node *node) {
    (void)dirfd;(void)user_path;(void)follow;
    memset(request,0,sizeof(*request));
    *out=(struct nas_vfs_path){.dentry=(struct nas_vfs_dentry *)(uintptr_t)1};
    *node=path_identity;return 0;
}
int nas_vfs_path_put(struct nas_vfs_path *path) {
    assert(path->dentry);path_drops++;path->dentry=NULL;return 0;
}
void nas_fs_path_end(struct nas_fs_path_request *request) {(void)request;}

#include "../kernel/fs/inotify.c"

static uint32_t value_u32(const uint8_t *buf,unsigned off) {
    uint32_t value;memcpy(&value,buf+off,sizeof(value));return value;
}
static void drain(unsigned slot) {
    uint8_t bytes[1024];
    while(nas_inotify_poll_slot(slot,1)) {
        uint64_t plan=nas_inotify_read_plan(slot,sizeof(bytes));
        assert(plan && plan<=sizeof(bytes));
        assert(nas_inotify_read(slot,bytes,plan)==plan);
    }
}
int main(void) {
    files.refs=1;task.files=&files;task.fds=&files.table;
    struct nas_vfs_mount *mount=(struct nas_vfs_mount *)(uintptr_t)0x12345000;
    path_identity=(struct nas_fs_node){.mount=mount,.mount_id=77,
                  .filesystem=(struct nas_vfs_type *)(uintptr_t)0x12346000,
                  .inode=(void *)(uintptr_t)0x12347000,.ino=42,.is_dir=true};
    const uint64_t fd=nas_inotify_init1(0x800ULL|0x80000ULL);
    assert(fd==5 && allocations==1 && files.table.entries[5].cloexec);
    assert(nas_inotify_init1(0x100ULL)==(uint64_t)-EINVAL);
    const uint32_t mask=NAS_IN_CREATE|NAS_IN_DELETE|NAS_IN_MODIFY|
                        NAS_IN_MOVED_FROM|NAS_IN_MOVED_TO;
    assert(nas_inotify_add_watch(fd,0x1000,mask)==1);
    assert(path_drops==1);
    assert(nas_inotify_add_watch(fd,0x1000,NAS_IN_MODIFY|IN_MASK_ADD)==1);
    assert(nas_inotify_add_watch(fd,0x1000,mask|IN_MASK_CREATE)==
           (uint64_t)-EEXIST);
    assert(nas_inotify_read_plan(33,4096)==(uint64_t)-EAGAIN);

    nas_inotify_emit_child(&path_identity,"alpha.txt",NAS_IN_CREATE,0,false);
    assert(nas_inotify_poll_slot(33,1)==1);
    assert(nas_inotify_read_plan(33,16)==(uint64_t)-EINVAL);
    uint8_t bytes[1024]={0};
    const uint64_t first=nas_inotify_read_plan(33,sizeof(bytes));
    assert(first==28);
    assert(nas_inotify_read(33,bytes,first)==first);
    assert(value_u32(bytes,0)==1);
    assert(value_u32(bytes,4)==NAS_IN_CREATE);
    assert(value_u32(bytes,12)==12);
    assert(!strcmp((const char *)bytes+16,"alpha.txt"));
    assert(!bytes[16+10] && !bytes[16+11]);

    const uint32_t cookie=nas_inotify_rename_cookie();
    nas_inotify_emit_child(&path_identity,"alpha.txt",NAS_IN_MOVED_FROM,cookie,false);
    nas_inotify_emit_child(&path_identity,"beta.txt",NAS_IN_MOVED_TO,cookie,false);
    assert(nas_inotify_read_plan(33,1024)==56);
    assert(nas_inotify_read(33,bytes,56)==56);
    assert(value_u32(bytes,8)==cookie && value_u32(bytes,36)==cookie);
    assert(value_u32(bytes,4)==NAS_IN_MOVED_FROM);
    assert(value_u32(bytes,32)==NAS_IN_MOVED_TO);

    /* Same inode after a mount incarnation recycle must NOT be a match. */
    struct nas_fs_node recycled=path_identity;
    recycled.mount_id++;
    nas_inotify_emit_child(&recycled,"not-an-event",NAS_IN_CREATE,0,false);
    assert(!nas_inotify_poll_slot(33,1));

    /* Overflow does not overwrite existing unread events. A single Linux
     * wd=-1/IN_Q_OVERFLOW sentinel follows after all 64 queued events. */
    for(unsigned i=0;i<80;i++) {
        char name[24];snprintf(name,sizeof(name),"child%03u",i);
        nas_inotify_emit_child(&path_identity,name,NAS_IN_CREATE,0,false);
    }
    assert(instances[33]->count==IN_EVENT_MAX && instances[33]->overflow_pending);
    unsigned overflow=0,create=0;
    while(nas_inotify_poll_slot(33,1)) {
        uint64_t planned=nas_inotify_read_plan(33,sizeof(bytes));
        assert(planned>=16 && planned<=sizeof(bytes));
        assert(nas_inotify_read(33,bytes,planned)==planned);
        for(unsigned pos=0;pos<planned;) {
            int32_t wd;memcpy(&wd,bytes+pos,4);
            uint32_t m=value_u32(bytes,pos+4),n=value_u32(bytes,pos+12);
            if(wd==-1) {assert(m==NAS_IN_Q_OVERFLOW);overflow++;}
            else {assert(m==NAS_IN_CREATE);create++;}
            pos+=16+n;
        }
    }
    assert(create==IN_EVENT_MAX && overflow==1);

    /* Final OpenFile release, not one descriptor close, reclaims watches.
     * A dup/fork alias keeps the SAME slot and pending notifications. */
    files.table.entries[6]=(struct nas_fd_entry){FD_INOTIFY,33,0};
    nas_open_files[33].refcount=2;
    files.table.entries[5]=(struct nas_fd_entry){0};
    nas_open_files[33].refcount--;
    nas_inotify_emit_child(&path_identity,"still-live",NAS_IN_MODIFY,0,false);
    assert(nas_inotify_poll_fd(6,1));
    drain(33);
    assert(nas_inotify_rm_watch(6,1)==0);
    assert(nas_inotify_read_plan(33,sizeof(bytes))==16);
    assert(nas_inotify_read(33,bytes,16)==16);
    assert(value_u32(bytes,4)==NAS_IN_IGNORED);
    assert(nas_inotify_rm_watch(6,1)==(uint64_t)-EINVAL);
    nas_open_files[33].refcount=0;
    nas_inotify_release(33);
    assert(!instances[33] && !allocations);
    puts("[M187 INOTIFY HOST PASS] Linux wire ABI, padding, rename cookies, stale mount identity, overflow, nonblock, read atomicity, poll, shared OpenFile final release");
    return 0;
}
