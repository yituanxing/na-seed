#include "pipe.h"
#include "fd.h"
#include "task.h"
#include "errno.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);

/* Pipe ring/object lifetime belongs here, not in syscall dispatch.
 * The open-file-description layer owns the endpoint reference counts.
 * This deliberately is NOT NaOS's mature pipefs/VFS implementation.
 */
struct nas_pipe {
    bool used;
    uint8_t bytes[NAS_PIPE_CAP];
    unsigned head, count, read_slot, write_slot;
};
static struct nas_pipe pipe_table[NAS_PIPE_COUNT];
/* Socketpair endpoints have one incoming and one outgoing channel, so
 * read/write ownership is tracked independently instead of duplicating the
 * ring implementation or inventing a second IPC lifetime model. */
static uint8_t pipe_read_owner[NAS_OPEN_FILE_LIMIT];
static uint8_t pipe_write_owner[NAS_OPEN_FILE_LIMIT];

static struct nas_pipe *pipe_for_slot(unsigned slot, bool writing) {
    if (slot >= NAS_OPEN_FILE_LIMIT) return 0;
    unsigned owner = writing ? pipe_write_owner[slot] : pipe_read_owner[slot];
    if (!owner || owner > NAS_PIPE_COUNT) return 0;
    struct nas_pipe *pipe = &pipe_table[owner - 1];
    return pipe->used ? pipe : 0;
}
static bool pipe_has_reader(const struct nas_pipe *pipe) {
    return pipe->read_slot < NAS_OPEN_FILE_LIMIT &&
           nas_open_files[pipe->read_slot].refcount != 0;
}
int nas_pipe_peer_slot(unsigned writing_slot) {
    const struct nas_pipe *pipe=pipe_for_slot(writing_slot,true);
    if(!pipe || !pipe_has_reader(pipe))return -ENOTCONN;
    return (int)pipe->read_slot;
}
static bool pipe_has_writer(const struct nas_pipe *pipe) {
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
    pipe->head = pipe->count = 0;
    pipe->read_slot = read_slot;
    pipe->write_slot = write_slot;
    pipe_read_owner[read_slot] = (uint8_t)(index + 1);
    pipe_write_owner[write_slot] = (uint8_t)(index + 1);
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
    }
    return rc;
}
void nas_pipe_release_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    /* Forget BOTH directions of a socket endpoint before slot reuse. */
    for (unsigned direction=0;direction<2;direction++) {
        bool writing=direction!=0;
        unsigned owner=writing?pipe_write_owner[slot]:pipe_read_owner[slot];
        if(!owner || owner>NAS_PIPE_COUNT)continue;
        struct nas_pipe *pipe=&pipe_table[owner-1];
        if(writing) {
            pipe->write_slot=NAS_OPEN_FILE_LIMIT;
            pipe_write_owner[slot]=0;
        } else {
            pipe->read_slot=NAS_OPEN_FILE_LIMIT;
            pipe_read_owner[slot]=0;
        }
        if (pipe->used && !pipe_has_reader(pipe) && !pipe_has_writer(pipe)) {
            pipe->used = false;
            pipe->count = pipe->head = 0;
        }
    }
}
bool nas_pipe_read_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    return !pipe || pipe->count || !pipe_has_writer(pipe);
}
bool nas_pipe_write_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    /* Waking for EPIPE is mandatory, not a false claim of writability. */
    return !pipe || pipe->count < NAS_PIPE_CAP || !pipe_has_reader(pipe);
}
bool nas_pipe_poll_readable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,false);
    return pipe && (pipe->count || !pipe_has_writer(pipe));
}
bool nas_pipe_poll_writable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    return pipe && pipe_has_reader(pipe) && pipe->count < NAS_PIPE_CAP;
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
    return pipe ? pipe->bytes[(pipe->head + offset) % NAS_PIPE_CAP] : 0;
}
void nas_pipe_read_commit(unsigned slot, unsigned count) {
    struct nas_pipe *pipe = pipe_for_slot(slot,false);
    if (!pipe) return;
    pipe->head = (pipe->head + count) % NAS_PIPE_CAP;
    pipe->count -= count;
}
int64_t nas_pipe_write_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (!pipe) return -EBADF;
    if (!requested) return 0;
    if (!pipe_has_reader(pipe)) return -EPIPE;
    if (pipe->count == NAS_PIPE_CAP)
        return nonblock ? -EAGAIN : -EOPNOTSUPP;
    unsigned space = NAS_PIPE_CAP - pipe->count;
    if (requested > space) {
        if (requested <= NAS_PIPE_CAP)
            return nonblock ? -EAGAIN : -EOPNOTSUPP;
        requested = space; /* > PIPE_BUF may make partial progress */
    }
    return (int64_t)requested;
}
void nas_pipe_write_byte(unsigned slot, unsigned offset, uint8_t value) {
    struct nas_pipe *pipe = pipe_for_slot(slot,true);
    if (!pipe) return;
    pipe->bytes[(pipe->head + pipe->count + offset) % NAS_PIPE_CAP] = value;
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
