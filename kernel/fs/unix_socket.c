#include "unix_socket.h"
#include "../fd.h"
#include "../pipe.h"
#include "../tmpfs.h"
#include "../bootfs.h"
#include "../errno.h"
#include "../lib/string.h"
#include "file_ops.h"

/* A listener owns not-yet-accepted open descriptions. Connect allocates
 * BOTH real stream directions before success, so a client can write before
 * accept without losing data. Accepted descriptors transfer (not copy) the
 * listener's kernel-held reference to a real process FD. */
#define NAS_UNIX_BACKLOG 16
enum { SOCK_FREE=0,SOCK_NEW,SOCK_BOUND,SOCK_LISTEN,SOCK_CONNECTED };
struct local_socket {
    unsigned state;
    uint64_t inode;
    unsigned first,count,queue[NAS_UNIX_BACKLOG];
    char name[256];
};
static struct local_socket sockets[NAS_OPEN_FILE_LIMIT];
struct socket_rights {
    unsigned count;
    uint8_t kinds[NAS_UNIX_MAX_RIGHTS],slots[NAS_UNIX_MAX_RIGHTS];
};
static struct socket_rights pending_rights[NAS_OPEN_FILE_LIMIT];
/* A queued SCM_RIGHTS ref lives in a kernel message, not in the sender's
 * FD table. Dispose of undelivered descriptions on socket final close. */
static void dispose_rights(unsigned slot) {
    struct socket_rights *r=&pending_rights[slot];
    for(unsigned i=0;i<r->count;i++) {
        unsigned owned=r->slots[i];
        if(owned>=NAS_OPEN_FILE_LIMIT || !nas_open_files[owned].refcount)
            continue;
        if(--nas_open_files[owned].refcount)continue;
        const struct nas_file_ops *ops=nas_open_files[owned].ops;
        if(ops && ops->release)ops->release(owned);
        nas_open_files[owned]=(struct nas_open_file){0};
    }
    *r=(struct socket_rights){0};
}
bool nas_unix_rights_available(unsigned sender,unsigned count) {
    int peer=nas_pipe_peer_slot(sender);
    return peer>=0 && count<=NAS_UNIX_MAX_RIGHTS &&
           pending_rights[peer].count==0;
}
int nas_unix_rights_send(unsigned sender,const uint8_t *kinds,
                         const uint8_t *slots,unsigned count) {
    if(!count)return 0;
    if(!nas_unix_rights_available(sender,count))return -EAGAIN;
    int peer=nas_pipe_peer_slot(sender);
    struct socket_rights *r=&pending_rights[peer];
    for(unsigned i=0;i<count;i++) {
        if(slots[i]>=NAS_OPEN_FILE_LIMIT ||
           !nas_open_files[slots[i]].refcount)return -EBADF;
    }
    for(unsigned i=0;i<count;i++) {
        r->kinds[i]=kinds[i];r->slots[i]=slots[i];
        nas_open_files[slots[i]].refcount++;
    }
    r->count=count;
    return 0;
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
        sockets[slot]=(struct local_socket){.state=SOCK_NEW};
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
        nas_pipe_release_description(pending);
        sockets[pending]=(struct local_socket){0};
        nas_open_files[pending]=(struct nas_open_file){0};
    }
    *s=(struct local_socket){0};
}
int nas_unix_socket_bind(unsigned slot,const char *path,uint32_t mode) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_NEW)
        return -EINVAL;
    if(!tmp_path(path) || !bootfs_mounted())return -EROFS;
    if(tmp_find(path) || bootfs_find(path))return -EADDRINUSE;
    int rc=tmp_parent_check(path);
    if(rc)return rc;
    struct nas_tmp_file *node=tmp_create(path,false,mode);
    if(!node)return -ENOSPC;
    node->mode=0140000U|(mode&07777U); /* genuine S_IFSOCK inode */
    struct local_socket *s=&sockets[slot];
    s->state=SOCK_BOUND;
    s->inode=tmp_number(node);
    memcpy(s->name,path,strlen(path)+1);
    return 0;
}
int nas_unix_socket_listen(unsigned slot,unsigned backlog) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_BOUND)
        return -EINVAL;
    if(!backlog)return -EINVAL;
    sockets[slot].state=SOCK_LISTEN;
    return 0;
}
int nas_unix_socket_connect(unsigned slot,const char *path) {
    if(slot>=NAS_OPEN_FILE_LIMIT || sockets[slot].state!=SOCK_NEW)
        return -EISCONN;
    struct nas_tmp_file *node=tmp_find(path);
    if(!node)return -ENOENT;
    if((node->mode&0170000U)!=0140000U)return -ECONNREFUSED;
    unsigned listener=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(sockets[i].state==SOCK_LISTEN &&
           sockets[i].inode==tmp_number(node)) {
            listener=i;break;
        }
    if(listener==NAS_OPEN_FILE_LIMIT)return -ECONNREFUSED;
    struct local_socket *s=&sockets[listener];
    if(s->count==NAS_UNIX_BACKLOG)return -EAGAIN;
    unsigned peer=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(!nas_open_files[i].refcount && i!=slot) {peer=i;break;}
    if(peer==NAS_OPEN_FILE_LIMIT)return -ENFILE;
    int rc=nas_pipe_create_socketpair(slot,peer);
    if(rc)return rc;
    nas_open_files[peer]=(struct nas_open_file){
        .refcount=1,.status_flags=2,
        .ops=nas_fs_ops_for_kind(FD_SOCKET)
    };
    sockets[peer]=(struct local_socket){.state=SOCK_CONNECTED};
    sockets[slot].state=SOCK_CONNECTED;
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
