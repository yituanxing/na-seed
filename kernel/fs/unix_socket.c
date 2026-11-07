#include "unix_socket.h"
#include "../fd.h"
#include "../pipe.h"
#include "../errno.h"
#include "../panic.h"
#include "../task.h"
#include "../lib/string.h"
#include "lookup.h"
#include "vfs.h"
#include "file_ops.h"
#include "syscall.h"

/* A listener owns not-yet-accepted open descriptions. Connect allocates
 * BOTH real stream directions before success, so a client can write before
 * accept without losing data. Accepted descriptors transfer (not copy) the
 * listener's kernel-held reference to a real process FD. */
#define NAS_UNIX_BACKLOG 16
enum { SOCK_FREE=0,SOCK_NEW,SOCK_BOUND,SOCK_LISTEN,SOCK_CONNECTED };
struct local_socket {
    unsigned state;
    /* Exactly one owned Path per bound shared open description. The Dentry
     * pins its backend inode and mount incarnation even after unlink or
     * rename, and dup/fork/SCM_RIGHTS never acquire another Path here. */
    struct nas_vfs_path bound;
    uint32_t owner_pid,peer_pid,peer_uid,peer_gid;
    bool has_peer;
    unsigned first,count,queue[NAS_UNIX_BACKLOG];
    char name[256];
};
static struct local_socket sockets[NAS_OPEN_FILE_LIMIT];
struct socket_rights {
    unsigned count;
    bool reserved;
    uint8_t kinds[NAS_UNIX_MAX_RIGHTS],slots[NAS_UNIX_MAX_RIGHTS];
};
static struct socket_rights pending_rights[NAS_OPEN_FILE_LIMIT];
/* A queued SCM_RIGHTS ref lives in a kernel message, not in the sender's
 * FD table. Dispose of undelivered descriptions on socket final close. */
static void dispose_rights(unsigned slot) {
    /* Clear queue before recursive final puts to avoid double disposal. */
    struct socket_rights held=pending_rights[slot];
    pending_rights[slot]=(struct socket_rights){0};
    for(unsigned i=0;i<held.count;i++)
        nas_fs_drop_kernel_description(held.slots[i]);
}
int nas_unix_rights_prepare(unsigned sender,const uint8_t *kinds,
                            const uint8_t *slots,unsigned count,
                            struct nas_unix_rights_transfer *transfer) {
    if(!transfer || !count || count>NAS_UNIX_MAX_RIGHTS || !kinds || !slots)
        return -EINVAL;
    *transfer=(struct nas_unix_rights_transfer){0};
    int peer=nas_pipe_peer_slot(sender);
    if(peer<0 || (unsigned)peer>=NAS_OPEN_FILE_LIMIT)return -ENOTCONN;
    if(pending_rights[peer].count || pending_rights[peer].reserved)
        return -EAGAIN;
    /* Both endpoints stay alive even when the payload writer parks and
     * another task closes/reuses its original descriptor. */
    if(sender>=NAS_OPEN_FILE_LIMIT || !nas_open_files[sender].refcount ||
       nas_open_files[sender].lease_token)return -EBADF;
    if(!nas_fd_hold_description(sender))return -ENFILE;
    if(!nas_fd_hold_description((unsigned)peer)) {
        nas_fs_drop_kernel_description(sender);
        return -ENFILE;
    }
    transfer->sender=sender;transfer->receiver=(unsigned)peer;
    transfer->active=true;
    for(unsigned i=0;i<count;i++) {
        unsigned slot=slots[i];
        if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
           nas_open_files[slot].lease_token) {
            nas_unix_rights_cancel(transfer);
            return -EBADF;
        }
        if(!nas_fd_hold_description(slot)) {
            nas_unix_rights_cancel(transfer);
            return -ENFILE;
        }
        transfer->kinds[i]=kinds[i];transfer->slots[i]=(uint8_t)slot;
        transfer->count++;
    }
    pending_rights[peer].reserved=true;
    return 0;
}
void nas_unix_rights_cancel(struct nas_unix_rights_transfer *transfer) {
    if(!transfer || !transfer->active)return;
    unsigned peer=transfer->receiver,sender=transfer->sender;
    pending_rights[peer].reserved=false;
    for(unsigned i=0;i<transfer->count;i++)
        nas_fs_drop_kernel_description(transfer->slots[i]);
    *transfer=(struct nas_unix_rights_transfer){0};
    nas_fs_drop_kernel_description(peer);
    nas_fs_drop_kernel_description(sender);
}
void nas_unix_rights_commit(struct nas_unix_rights_transfer *transfer) {
    if(!transfer || !transfer->active)return;
    struct socket_rights *r=&pending_rights[transfer->receiver];
    if(!r->reserved || r->count)nas_fail("SCM_RIGHTS reservation lost");
    /* Already-pinned refs move to the receiver's kernel-owned message;
     * no refcount increase and no late failure after data was written. */
    for(unsigned i=0;i<transfer->count;i++) {
        r->kinds[i]=transfer->kinds[i];r->slots[i]=transfer->slots[i];
    }
    r->count=transfer->count;
    r->reserved=false;
    unsigned peer=transfer->receiver,sender=transfer->sender;
    *transfer=(struct nas_unix_rights_transfer){0};
    nas_fs_drop_kernel_description(peer);
    nas_fs_drop_kernel_description(sender);
}
unsigned nas_unix_rights_count(unsigned receiver) {
    return receiver<NAS_OPEN_FILE_LIMIT ? pending_rights[receiver].count : 0;
}
unsigned nas_unix_rights_receive(unsigned receiver,uint8_t *kinds,
                                 uint8_t *slots,unsigned max_count) {
    if(receiver>=NAS_OPEN_FILE_LIMIT)return 0;
    struct socket_rights *r=&pending_rights[receiver];
    unsigned count=r->count<max_count?r->count:max_count;
    for(unsigned i=0;i<count;i++) {
        kinds[i]=r->kinds[i];slots[i]=r->slots[i];
    }
    /* Transferred refs for delivered descriptors are now process-owned.
     * Any excess rights remain kernel-owned until socket teardown. */
    for(unsigned i=count;i<r->count;i++) {
        r->slots[i-count]=r->slots[i];
        r->kinds[i-count]=r->kinds[i];
    }
    r->count-=count;
    return count;
}

void nas_unix_socket_init(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)
        sockets[slot]=(struct local_socket){
            .state=SOCK_NEW,.owner_pid=(uint32_t)nas_current_task->tgid
        };
}
void nas_unix_socket_pair_ready(unsigned first,unsigned second) {
    /* Both ends of socketpair() are owned by the creating process. */
    uint32_t pid=(uint32_t)nas_current_task->tgid;
    sockets[first]=(struct local_socket){
        .state=SOCK_CONNECTED,.owner_pid=pid,.peer_pid=pid,.has_peer=true
    };
    sockets[second]=(struct local_socket){
        .state=SOCK_CONNECTED,.owner_pid=pid,.peer_pid=pid,.has_peer=true
    };
}
void nas_unix_socket_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    dispose_rights(slot);
    struct local_socket *s=&sockets[slot];
    /* Close pending accepted endpoints only when final listener description
     * is closed, preserving the same refcount lifecycle as fork and dup. */
    for(unsigned i=0;i<s->count;i++) {
        unsigned pending=s->queue[(s->first+i)%NAS_UNIX_BACKLOG];
        if(pending>=NAS_OPEN_FILE_LIMIT)continue;
        /* Pending accept endpoint has one kernel reference and may
         * contain undelivered SCM_RIGHTS; its full backend finalizer owns
         * both transport and message references. */
        nas_fs_drop_kernel_description(pending);
    }
    if(s->bound.dentry && nas_vfs_path_put(&s->bound))
        nas_fail("UNIX listener final close lost owned inode Path");
    *s=(struct local_socket){0};
}
int nas_unix_socket_bind(unsigned slot,const struct nas_vfs_path *parent,
                         const char *leaf,uint32_t mode,
                         const char *display_name) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_NEW)
        return -EINVAL;
    if(!parent || !parent->dentry || !leaf || !*leaf || !display_name)
        return -EINVAL;
    size_t name_len=strnlen(display_name,sizeof(sockets[slot].name));
    if(name_len>=sizeof(sockets[slot].name))return -ENAMETOOLONG;
    struct nas_vfs_path created={0};
    int rc=nas_vfs_mknod_child(parent,leaf,0140000U|(mode&07777U),&created);
    /* Linux bind(2) reports a conflicting socket filename as EADDRINUSE,
     * while the generic VFS correctly reports an occupied name as EEXIST. */
    if(rc==-EEXIST)return -EADDRINUSE;
    if(rc)return rc;
    struct local_socket *s=&sockets[slot];
    s->state=SOCK_BOUND;
    s->bound=created;
    memcpy(s->name,display_name,name_len+1);
    return 0;
}
int nas_unix_socket_listen(unsigned slot,unsigned backlog) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_BOUND)
        return -EINVAL;
    if(!backlog)return -EINVAL;
    sockets[slot].state=SOCK_LISTEN;
    sockets[slot].owner_pid=(uint32_t)nas_current_task->tgid;
    return 0;
}
static bool same_inode(const struct nas_fs_node *a,
                       const struct nas_fs_node *b) {
    return a->mount==b->mount && a->mount_id==b->mount_id &&
           a->filesystem==b->filesystem && a->inode==b->inode &&
           a->ino==b->ino;
}
int nas_unix_socket_connect(unsigned slot,const struct nas_vfs_path *target) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_NEW)
        return -EISCONN;
    struct nas_fs_node node={0};
    int rc=nas_vfs_path_node(target,&node);
    if(rc)return rc;
    if((node.mode&0170000U)!=0140000U)return -ECONNREFUSED;
    unsigned listener=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        if(sockets[i].state!=SOCK_LISTEN || !sockets[i].bound.dentry)continue;
        struct nas_fs_node bound={0};
        if(nas_vfs_path_node(&sockets[i].bound,&bound))continue;
        if(same_inode(&bound,&node)) {
            listener=i;break;
        }
    }
    if(listener==NAS_OPEN_FILE_LIMIT)return -ECONNREFUSED;
    struct local_socket *s=&sockets[listener];
    if(s->count==NAS_UNIX_BACKLOG)return -EAGAIN;
    unsigned peer=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(!nas_open_files[i].refcount && i!=slot) {peer=i;break;}
    if(peer==NAS_OPEN_FILE_LIMIT)return -ENFILE;
    rc=nas_pipe_create_socketpair(slot,peer);
    if(rc)return rc;
    nas_open_files[peer]=(struct nas_open_file){
        .refcount=1,.status_flags=2,
        .ops=nas_fs_ops_for_kind(FD_SOCKET)
    };
    uint32_t client_pid=(uint32_t)nas_current_task->tgid;
    sockets[peer]=(struct local_socket){
        .state=SOCK_CONNECTED,.owner_pid=s->owner_pid,
        .peer_pid=client_pid,.has_peer=true
    };
    sockets[slot].state=SOCK_CONNECTED;
    sockets[slot].peer_pid=s->owner_pid;
    sockets[slot].peer_uid=0;
    sockets[slot].peer_gid=0;
    sockets[slot].has_peer=true;
    s->queue[(s->first+s->count)%NAS_UNIX_BACKLOG]=peer;
    s->count++;
    return 0;
}
int nas_unix_socket_accept(unsigned slot,unsigned *peer_slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_LISTEN)
        return -EINVAL;
    struct local_socket *s=&sockets[slot];
    if(!s->count)return -EAGAIN;
    *peer_slot=s->queue[s->first];
    s->first=(s->first+1)%NAS_UNIX_BACKLOG;
    s->count--;
    return 0;
}
bool nas_unix_socket_listening(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].state==SOCK_LISTEN;
}
bool nas_unix_socket_readable(unsigned slot) {
    return nas_unix_socket_listening(slot) && sockets[slot].count!=0;
}
const char *nas_unix_socket_name(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT &&
        (sockets[slot].state==SOCK_BOUND ||
         sockets[slot].state==SOCK_LISTEN) ? sockets[slot].name : 0;
}
int nas_unix_socket_peercred(unsigned slot,uint32_t credentials[3]) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !sockets[slot].has_peer)
        return -ENOTCONN;
    credentials[0]=sockets[slot].peer_pid;
    credentials[1]=sockets[slot].peer_uid;
    credentials[2]=sockets[slot].peer_gid;
    return 0;
}
