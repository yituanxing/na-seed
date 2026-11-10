#include "pipe.h"
#include "fd.h"
#include "task.h"
#include "errno.h"
#include "mm/heap.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);

/* Pipe ring/object lifetime belongs here, not in syscall dispatch.
 * The open-file-description layer owns the endpoint reference counts.
 * This deliberately is NOT NaOS's mature pipefs/VFS implementation.
 */
struct nas_pipe {
    bool used;
    bool socket_channel;
    bool named;
    uint64_t named_mount_id,named_ino;
    uint8_t bytes[NAS_PIPE_CAP];
    uint8_t *data;
    unsigned capacity;
    unsigned head, count, read_slot, write_slot;
};
static struct nas_pipe pipe_table[NAS_PIPE_COUNT];
/* Socketpair endpoints have one incoming and one outgoing channel, so
 * read/write ownership is tracked independently instead of duplicating the
 * ring implementation or inventing a second IPC lifetime model. */
static uint8_t pipe_read_owner[NAS_OPEN_FILE_LIMIT];
static uint8_t pipe_write_owner[NAS_OPEN_FILE_LIMIT];
static bool socket_read_closed[NAS_OPEN_FILE_LIMIT];
static bool socket_write_closed[NAS_OPEN_FILE_LIMIT];

static struct nas_pipe *pipe_for_slot(unsigned slot, bool writing) {
    if (slot >= NAS_OPEN_FILE_LIMIT) return 0;
    unsigned owner = writing ? pipe_write_owner[slot] : pipe_read_owner[slot];
    if (!owner || owner > NAS_PIPE_COUNT) return 0;
    struct nas_pipe *pipe = &pipe_table[owner - 1];
    return pipe->used ? pipe : 0;
}
static struct nas_pipe *pipe_for_end(unsigned slot) {
    struct nas_pipe *pipe = pipe_for_slot(slot, false);
    return pipe ? pipe : pipe_for_slot(slot, true);
}
int64_t nas_pipe_capacity(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_end(slot);
    return pipe && !pipe->socket_channel ? (int64_t)pipe->capacity : -EBADF;
}
int64_t nas_pipe_set_capacity(unsigned slot, int32_t requested) {
    struct nas_pipe *pipe = pipe_for_end(slot);
    if (!pipe || pipe->socket_channel) return -EBADF;
    if (requested < 0) return -EINVAL;
    unsigned target = NAS_PIPE_CAP;
    if ((unsigned)requested > NAS_PIPE_MAX_CAP) return -EPERM;
    /* Linux rounds upward to the next power-of-two multiple of a page.
     * The 4 KiB minimum also applies to a zero-byte request. */
    while (target < (unsigned)requested) target <<= 1;
    if (target == pipe->capacity) return (int64_t)target;
    if (target < pipe->count) return -EBUSY;

    /* The embedded default buffer avoids 32 tiny heap allocations at boot;
     * growth is transactional and may fail without touching live data. */
    uint8_t *next = target == NAS_PIPE_CAP ? pipe->bytes : nas_heap_alloc(target);
    if (!next) return -ENOMEM;
    for (unsigned i = 0; i < pipe->count; i++)
        next[i] = pipe->data[(pipe->head + i) % pipe->capacity];
    uint8_t *old = pipe->data;
    pipe->data = next;
    pipe->capacity = target;
    pipe->head = 0;
    if (old != pipe->bytes) (void)nas_heap_free(old);
    return (int64_t)target;
}
static bool pipe_has_reader(const struct nas_pipe *pipe) {
    if(pipe->named) {
        unsigned owner=(unsigned)(pipe-pipe_table)+1U;
        for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++)
            if(pipe_read_owner[slot]==owner &&
               nas_open_files[slot].refcount)return true;
        return false;
    }
    return pipe->read_slot < NAS_OPEN_FILE_LIMIT &&
           nas_open_files[pipe->read_slot].refcount != 0;
}
int nas_pipe_peer_slot(unsigned writing_slot) {
    const struct nas_pipe *pipe=pipe_for_slot(writing_slot,true);
    if(!pipe || !pipe_has_reader(pipe))return -ENOTCONN;
    return (int)pipe->read_slot;
}
static bool pipe_has_writer(const struct nas_pipe *pipe) {
    if(pipe->named) {
        unsigned owner=(unsigned)(pipe-pipe_table)+1U;
        for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++)
            if(pipe_write_owner[slot]==owner &&
               nas_open_files[slot].refcount)return true;
        return false;
    }
    return pipe->write_slot < NAS_OPEN_FILE_LIMIT &&
           nas_open_files[pipe->write_slot].refcount != 0;
}
int nas_pipe_create(unsigned read_slot, unsigned write_slot) {
    if (read_slot >= NAS_OPEN_FILE_LIMIT || write_slot >= NAS_OPEN_FILE_LIMIT ||
        read_slot == write_slot || pipe_read_owner[read_slot] ||
        pipe_write_owner[write_slot]) return -EINVAL;
    unsigned index = NAS_PIPE_COUNT;
    for (unsigned i = 0; i < NAS_PIPE_COUNT; i++)
        if (!pipe_table[i].used) { index = i; break; }
    if (index == NAS_PIPE_COUNT) {
        nas_print("[PIPE CAP] active-pipes="); nas_dec(NAS_PIPE_COUNT);
        nas_print(" capacity="); nas_dec(NAS_PIPE_COUNT); nas_print("\n");
        return -ENFILE;
    }
    struct nas_pipe *pipe = &pipe_table[index];
    pipe->used = true;
    pipe->socket_channel = false;
    pipe->named = false;
    pipe->named_mount_id=pipe->named_ino=0;
    pipe->data = pipe->bytes;
    pipe->capacity = NAS_PIPE_CAP;
    pipe->head = pipe->count = 0;
    pipe->read_slot = read_slot;
    pipe->write_slot = write_slot;
    pipe_read_owner[read_slot] = (uint8_t)(index + 1);
    pipe_write_owner[write_slot] = (uint8_t)(index + 1);
    return 0;
}
int nas_pipe_named_attach(uint64_t mount_id,uint64_t ino,
                          unsigned slot,unsigned access,bool nonblock) {
    if(!mount_id || !ino || slot>=NAS_OPEN_FILE_LIMIT || access>2U ||
       pipe_read_owner[slot] || pipe_write_owner[slot])return -EINVAL;
    unsigned index=NAS_PIPE_COUNT;
    for(unsigned i=0;i<NAS_PIPE_COUNT;i++)
        if(pipe_table[i].used && pipe_table[i].named &&
           pipe_table[i].named_mount_id==mount_id &&
           pipe_table[i].named_ino==ino) {index=i;break;}
    if(index==NAS_PIPE_COUNT) {
        for(unsigned i=0;i<NAS_PIPE_COUNT;i++)
            if(!pipe_table[i].used) {index=i;break;}
        if(index==NAS_PIPE_COUNT)return -ENFILE;
    }
    struct nas_pipe *pipe=&pipe_table[index];
    const bool has_readers=pipe->used && pipe_has_reader(pipe);
    const bool has_writers=pipe->used && pipe_has_writer(pipe);
    if(access==1U && !has_readers)
        return nonblock ? -ENXIO : -EOPNOTSUPP;
    /* Blocking open(2) must park for a peer before publishing its FD. The
     * existing pending-open path has no FIFO-specific waiter yet. Reject
     * this exact unsupported state rather than falsely succeeding as if
     * this were O_NONBLOCK. Linux O_RDWR does not wait for a peer. */
    if(access==0U && !nonblock && !has_writers)
        return -EOPNOTSUPP;
    if(!pipe->used) {
        pipe->used=true;
        pipe->socket_channel=false;
        pipe->named=true;
        pipe->named_mount_id=mount_id;
        pipe->named_ino=ino;
        pipe->data=pipe->bytes;
        pipe->capacity=NAS_PIPE_CAP;
        pipe->head=pipe->count=0;
        pipe->read_slot=pipe->write_slot=NAS_OPEN_FILE_LIMIT;
    }
    if(access!=1U)pipe_read_owner[slot]=(uint8_t)(index+1U);
    if(access!=0U)pipe_write_owner[slot]=(uint8_t)(index+1U);
    return 0;
}
int nas_pipe_create_socketpair(unsigned first_slot,unsigned second_slot) {
    if(first_slot==second_slot)return -EINVAL;
    /* Each socket reads the peer's writes: A->B and B->A. Reserve both
     * channels before exposing either descriptor to userspace. */
    int rc=nas_pipe_create(second_slot,first_slot);
    if(rc)return rc;
    rc=nas_pipe_create(first_slot,second_slot);
    if(rc) {
        nas_pipe_release_description(first_slot);
        nas_pipe_release_description(second_slot);
    } else {
        pipe_for_slot(first_slot,true)->socket_channel=true;
        pipe_for_slot(second_slot,true)->socket_channel=true;
        socket_read_closed[first_slot]=socket_read_closed[second_slot]=false;
        socket_write_closed[first_slot]=socket_write_closed[second_slot]=false;
    }
    return rc;
}
static void pipe_release_direction(unsigned slot,bool writing) {
    unsigned owner=writing?pipe_write_owner[slot]:pipe_read_owner[slot];
    if(!owner || owner>NAS_PIPE_COUNT)return;
    struct nas_pipe *pipe=&pipe_table[owner-1];
    if(writing) {
        if(!pipe->named)pipe->write_slot=NAS_OPEN_FILE_LIMIT;
        pipe_write_owner[slot]=0;
    } else {
        if(!pipe->named)pipe->read_slot=NAS_OPEN_FILE_LIMIT;
        pipe_read_owner[slot]=0;
    }
    bool has_attached=false;
    if(pipe->named) {
        unsigned tag=owner;
        for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
            if(pipe_read_owner[i]==tag || pipe_write_owner[i]==tag) {
                has_attached=true;break;
            }
    } else has_attached=pipe_has_reader(pipe)||pipe_has_writer(pipe);
    if(pipe->used && !has_attached) {
        if (pipe->data != pipe->bytes) (void)nas_heap_free(pipe->data);
        pipe->data=pipe->bytes;
        pipe->capacity=NAS_PIPE_CAP;
        pipe->used=false;
        pipe->named=false;
        pipe->named_mount_id=pipe->named_ino=0;
        pipe->count=pipe->head=0;
    }
}
int nas_pipe_socket_shutdown(unsigned slot,unsigned how) {
    if(slot>=NAS_OPEN_FILE_LIMIT || how>2U)return -EINVAL;
    if(!pipe_read_owner[slot] && !pipe_write_owner[slot] &&
       !socket_read_closed[slot] && !socket_write_closed[slot])
        return -ENOTCONN;
    if((how==0U || how==2U) && !socket_read_closed[slot]) {
        socket_read_closed[slot]=true;
        pipe_release_direction(slot,false);
    }
    if((how==1U || how==2U) && !socket_write_closed[slot]) {
        socket_write_closed[slot]=true;
        pipe_release_direction(slot,true);
    }
    return 0;
}
bool nas_pipe_socket_read_shutdown(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && socket_read_closed[slot];
}
bool nas_pipe_socket_write_shutdown(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && socket_write_closed[slot];
}
void nas_pipe_release_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    /* Forget both directions on final File close; half-close leaves its
     * peer end and buffered stream bytes alone until they can be consumed. */
    pipe_release_direction(slot,false);
    pipe_release_direction(slot,true);
    socket_read_closed[slot]=socket_write_closed[slot]=false;
}
bool nas_pipe_read_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    return !pipe || pipe->count || !pipe_has_writer(pipe);
}
bool nas_pipe_write_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    /* Waking for EPIPE is mandatory, not a false claim of writability. */
    return !pipe || pipe->count < pipe->capacity || !pipe_has_reader(pipe);
}
bool nas_pipe_poll_readable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    return pipe && (pipe->count || !pipe_has_writer(pipe));
}
bool nas_pipe_read_hup(unsigned slot) {
    const struct nas_pipe *pipe=pipe_for_slot(slot,false);
    return pipe && !pipe_has_writer(pipe);
}
bool nas_pipe_poll_writable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    return pipe && pipe_has_reader(pipe) && pipe->count < pipe->capacity;
}
int64_t nas_pipe_read_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    if (!pipe) return -EBADF;
    if (!requested) return 0;
    if (!pipe->count)
        return !pipe_has_writer(pipe) ? 0 :
            nonblock ? -EAGAIN : -EOPNOTSUPP;
    if (requested > pipe->count) requested = pipe->count;
    return (int64_t)requested;
}
uint8_t nas_pipe_read_byte(unsigned slot, unsigned offset) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    return pipe ? pipe->data[(pipe->head + offset) % pipe->capacity] : 0;
}
void nas_pipe_read_commit(unsigned slot, unsigned count) {
    struct nas_pipe *pipe = pipe_for_slot(slot,false);
    if (!pipe) return;
    pipe->head = (pipe->head + count) % pipe->capacity;
    pipe->count -= count;
}
int64_t nas_pipe_write_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (!pipe) return -EBADF;
    if (!requested) return 0;
    if (!pipe_has_reader(pipe)) return -EPIPE;
    if (pipe->count == pipe->capacity)
        return nonblock ? -EAGAIN : -EOPNOTSUPP;
    unsigned space = pipe->capacity - pipe->count;
    if (requested > space) {
        if (requested <= NAS_PIPE_CAP)
            return nonblock ? -EAGAIN : -EOPNOTSUPP;
        requested = space; /* > PIPE_BUF may make partial progress */
    }
    return (int64_t)requested;
}
int64_t nas_socket_write_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (!pipe) return -EBADF;
    if (!requested) return 0;
    if (!pipe_has_reader(pipe)) return -EPIPE;
    /* The ring is a shared implementation detail, not a promise that
     * AF_UNIX stream writes <= PIPE_BUF are atomic. Unlike pipe writes,
     * a stream sender may advance as soon as ANY receive space exists.
     * The public File data plane preserves the committed short count. */
    unsigned space = pipe->capacity - pipe->count;
    if (!space) return nonblock ? -EAGAIN : -EOPNOTSUPP;
    if (requested > space) requested = space;
    return (int64_t)requested;
}
void nas_pipe_write_byte(unsigned slot, unsigned offset, uint8_t value) {
    struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (!pipe) return;
    pipe->data[(pipe->head + pipe->count + offset) % pipe->capacity] = value;
}
void nas_pipe_write_commit(unsigned slot, unsigned count) {
    struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (pipe) pipe->count += count;
}
bool nas_pipe_snapshot(unsigned index, unsigned *bytes,
                       bool *readers, bool *writers) {
    if (index >= NAS_PIPE_COUNT || !pipe_table[index].used) return false;
    const struct nas_pipe *pipe = &pipe_table[index];
    if (bytes) *bytes = pipe->count;
    if (readers) *readers = pipe_has_reader(pipe);
    if (writers) *writers = pipe_has_writer(pipe);
    return true;
}
