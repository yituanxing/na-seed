/* AF_UNIX M107: call the REAL NaSeed listener/SCM_RIGHTS implementation.
 * Only the external VFS Path provider, pipe allocation and file-description
 * lifecycle edges are deterministic fakes. VFS creation/namei itself has
 * an independent real-TU regression in test-vfs-provider.c.
 *
 * This deliberately models two mount incarnations with identical backend
 * inode numbers: listener selection must compare full inode ownership, not
 * the old tmpfs-only inode number or the original pathname snapshot. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/errno.h"
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/fs/unix_socket.h"

struct nas_task host_task={.tgid=42};
struct nas_task *nas_current_task=&host_task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static unsigned path_drops,peer_drops,pipe_pairs;
static const int mount_a=1,mount_b=2;
static struct nas_vfs_type provider={.name="mock-vfs-inodes"};
struct fake_inode {
    bool allocated,visible;
    uint64_t ino;
    uint32_t mode;
    unsigned refs;
    char name[40];
};
static struct fake_inode root={.allocated=true,.visible=true,.refs=1};
static struct fake_inode inodes[12];
static uint64_t serial=19;
static unsigned paired[NAS_OPEN_FILE_LIMIT];

static struct nas_vfs_path parent_path(void) {
    return (struct nas_vfs_path){
        .mount=(const struct nas_vfs_mount *)&mount_a,
        .mount_id=123,
        .dentry=(struct nas_vfs_dentry *)&root
    };
}
static struct fake_inode *raw_inode(const struct nas_vfs_path *p) {
    if(!p || !p->dentry || p->dentry==(struct nas_vfs_dentry *)&root)return NULL;
    struct fake_inode *inode=(struct fake_inode *)p->dentry;
    return inode>=inodes && inode<inodes+12 ? inode : NULL;
}
int nas_vfs_path_node(const struct nas_vfs_path *path,struct nas_fs_node *out) {
    struct fake_inode *inode=raw_inode(path);
    if(!inode || !inode->allocated || !out)return -ESTALE;
    *out=(struct nas_fs_node){
        .inode=inode,.ino=inode->ino,.mode=inode->mode,
        .mount=path->mount,.mount_id=path->mount_id,
        .filesystem=&provider
    };
    return 0;
}
int nas_vfs_mknod_child(const struct nas_vfs_path *parent,const char *leaf,
                        uint64_t mode,struct nas_vfs_path *created) {
    if(!parent || parent->dentry!=(struct nas_vfs_dentry *)&root ||
       !created || !leaf || !*leaf)return -EINVAL;
    *created=(struct nas_vfs_path){0};
    if((mode&0170000U)!=0140000U)return -EOPNOTSUPP;
    for(unsigned i=0;i<12;i++)
        if(inodes[i].visible && !strcmp(inodes[i].name,leaf))return -EEXIST;
    for(unsigned i=0;i<12;i++) {
        if(inodes[i].allocated)continue;
        inodes[i]=(struct fake_inode){
            .allocated=true,.visible=true,.ino=++serial,
            .mode=(uint32_t)mode,.refs=1
        };
        assert(strlen(leaf)<sizeof(inodes[i].name));
        strcpy(inodes[i].name,leaf);
        *created=(struct nas_vfs_path){
            .mount=parent->mount,.mount_id=parent->mount_id,
            .dentry=(struct nas_vfs_dentry *)&inodes[i]
        };
        return 0;
    }
    return -ENOSPC;
}
int nas_vfs_path_put(struct nas_vfs_path *path) {
    struct fake_inode *inode=raw_inode(path);
    if(!inode || !inode->refs)return -EINVAL;
    inode->refs--;
    path_drops++;
    if(!inode->refs && !inode->visible)inode->allocated=false;
    *path=(struct nas_vfs_path){0};
    return 0;
}
static int fake_lookup(const char *name,struct nas_vfs_path *out) {
    for(unsigned i=0;i<12;i++) {
        if(inodes[i].visible && !strcmp(inodes[i].name,name)) {
            inodes[i].refs++;
            *out=(struct nas_vfs_path){
                .mount=(const struct nas_vfs_mount *)&mount_a,
                .mount_id=123,.dentry=(struct nas_vfs_dentry *)&inodes[i]
            };
            return 0;
        }
    }
    return -ENOENT;
}
static void fake_unlink(const char *name) {
    struct nas_vfs_path path={0};
    assert(fake_lookup(name,&path)==0);
    struct fake_inode *inode=raw_inode(&path);
    assert(inode && inode->visible);
    inode->visible=false;
    assert(nas_vfs_path_put(&path)==0);
}
static void fake_rename(const char *old_name,const char *new_name) {
    struct nas_vfs_path path={0};
    assert(fake_lookup(old_name,&path)==0);
    struct fake_inode *inode=raw_inode(&path);
    assert(inode && strlen(new_name)<sizeof(inode->name));
    strcpy(inode->name,new_name);
    assert(nas_vfs_path_put(&path)==0);
}
int nas_pipe_create_socketpair(unsigned first,unsigned second) {
    if(first>=NAS_OPEN_FILE_LIMIT || second>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    paired[first]=second;paired[second]=first;
    pipe_pairs++;
    return 0;
}
int nas_pipe_peer_slot(unsigned sender) {
    if(sender>=NAS_OPEN_FILE_LIMIT || paired[sender]>=NAS_OPEN_FILE_LIMIT)
        return -1;
    return (int)paired[sender];
}
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    (void)kind;return NULL;
}
void nas_fs_drop_kernel_description(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount);
    if(!--nas_open_files[slot].refcount) {
        peer_drops++;
        nas_unix_socket_release(slot);
    }
}
void nas_print(const char *message){(void)message;}
void nas_arch_halt(void){assert(!"NaSeed unix backend panic");__builtin_trap();}

static void new_endpoint(unsigned slot) {
    assert(slot<NAS_OPEN_FILE_LIMIT && !nas_open_files[slot].refcount);
    nas_open_files[slot].refcount=1;
    nas_unix_socket_init(slot);
}
static void connect_named(unsigned slot,const char *name) {
    struct nas_vfs_path found={0};
    assert(fake_lookup(name,&found)==0);
    assert(nas_unix_socket_connect(slot,&found)==0);
    assert(nas_vfs_path_put(&found)==0);
}
int main(void) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)paired[i]=NAS_OPEN_FILE_LIMIT;
    struct nas_vfs_path parent=parent_path(),found={0};
    new_endpoint(10);
    assert(nas_unix_socket_bind(10,&parent,"sock",0777,"sock")==0);
    assert(!strcmp(nas_unix_socket_name(10),"sock"));
    assert(nas_unix_socket_bind(10,&parent,"sock",0777,"sock")==-EINVAL);
    assert(nas_unix_socket_listen(10,8)==0);
    new_endpoint(11);connect_named(11,"sock");
    unsigned accepted=NAS_OPEN_FILE_LIMIT;
    assert(nas_unix_socket_accept(10,&accepted)==0);
    assert(accepted<NAS_OPEN_FILE_LIMIT && nas_open_files[accepted].refcount==1);

    /* Original listener stays live with unlink'd path and queued streams.
     * A new bind with the same basename gets a different backend inode. */
    fake_unlink("sock");
    assert(fake_lookup("sock",&found)==-ENOENT);
    new_endpoint(12);
    assert(nas_unix_socket_bind(12,&parent,"sock",0700,"sock")==0);
    assert(nas_unix_socket_listen(12,8)==0);
    new_endpoint(13);connect_named(13,"sock");
    assert(!nas_unix_socket_readable(10));
    assert(nas_unix_socket_readable(12));
    unsigned fresh=NAS_OPEN_FILE_LIMIT;
    assert(nas_unix_socket_accept(12,&fresh)==0);
    assert(accepted!=fresh);

    /* Rename does not alter inode identity. Connect by the new name works;
     * supplying the old string is irrelevant once path lookup is owned. */
    fake_rename("sock","renamed");
    assert(fake_lookup("sock",&found)==-ENOENT);
    new_endpoint(14);connect_named(14,"renamed");
    assert(nas_unix_socket_readable(12));

    /* Even the very SAME backend pointer/number must not match a listener
     * reached under a different mount incarnation. */
    new_endpoint(15);
    assert(fake_lookup("renamed",&found)==0);
    found.mount=(const struct nas_vfs_mount *)&mount_b;
    found.mount_id=456;
    assert(nas_unix_socket_connect(15,&found)==-ECONNREFUSED);
    assert(nas_vfs_path_put(&found)==0);
    assert(nas_unix_socket_readable(12));

    /* Duplicated listener FD points to ONE open description and ONE Path.
     * First close retains reachability; only final close puts the Path. */
    nas_open_files[12].refcount++;
    unsigned before_drop=path_drops;
    nas_fs_drop_kernel_description(12);
    assert(path_drops==before_drop && nas_unix_socket_listening(12));
    assert(nas_unix_socket_name(12));
    nas_fs_drop_kernel_description(12);
    assert(path_drops==before_drop+1 && !nas_unix_socket_listening(12));
    fake_unlink("renamed");
    assert(fake_lookup("renamed",&found)==-ENOENT);

    /* SCM_RIGHTS holds an independent reference after the sender closes.
     * The receiver takes ownership, no double put on its final close. */
    new_endpoint(40);new_endpoint(41);
    nas_unix_socket_pair_ready(40,41);
    paired[40]=41;paired[41]=40;
    uint8_t kind=FD_SOCKET,transfer_slot=14;
    uint16_t prior=nas_open_files[transfer_slot].refcount;
    assert(nas_unix_rights_send(40,&kind,&transfer_slot,1)==0);
    assert(nas_open_files[transfer_slot].refcount==prior+1);
    nas_fs_drop_kernel_description(transfer_slot); /* sender closes */
    uint8_t got_kind=0,got_slot=0;
    assert(nas_unix_rights_receive(41,&got_kind,&got_slot,1)==1);
    assert(got_kind==FD_SOCKET && got_slot==transfer_slot);
    assert(nas_open_files[transfer_slot].refcount==prior);
    nas_fs_drop_kernel_description(got_slot); /* receiver closes */
    assert(nas_open_files[transfer_slot].refcount==0);
    nas_fs_drop_kernel_description(40);
    nas_fs_drop_kernel_description(41);

    /* Release old (unlinked) listener only after its final description.
     * Connection peers survive independent of its bound name. */
    unsigned released_before=path_drops;
    nas_fs_drop_kernel_description(10);
    assert(path_drops==released_before+1);
    assert(!nas_unix_socket_listening(10));
    assert(pipe_pairs>=3 && peer_drops>=3);
    puts("[M107 AF_UNIX KERNEL PASS] real listener/rights backend: inode/mount identity, unlink/rebind, rename, dup/final pin, SCM_RIGHTS");
    return 0;
}
