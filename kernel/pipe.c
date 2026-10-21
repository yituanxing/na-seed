#include "pipe.h"
#include "fd.h"
#include "task.h"
#include "errno.h"

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
static uint8_t pipe_slot_owner[NAS_FD_LIMIT]; /* one-based pipe index */

static struct nas_pipe *pipe_for_slot(unsigned slot) {
    if (slot >= NAS_FD_LIMIT) return 0;
    unsigned owner = pipe_slot_owner[slot];
    if (!owner || owner > NAS_PIPE_COUNT) return 0;
    struct nas_pipe *pipe = &pipe_table[owner - 1];
    return pipe->used ? pipe : 0;
}
static bool pipe_has_reader(const struct nas_pipe *pipe) {
    return pipe->read_slot < NAS_FD_LIMIT &&
           nas_file_refcount[pipe->read_slot] != 0;
}
static bool pipe_has_writer(const struct nas_pipe *pipe) {
    return pipe->write_slot < NAS_FD_LIMIT &&
           nas_file_refcount[pipe->write_slot] != 0;
}
int nas_pipe_create(unsigned read_slot, unsigned write_slot) {
    if (read_slot >= NAS_FD_LIMIT || write_slot >= NAS_FD_LIMIT ||
        read_slot == write_slot || pipe_slot_owner[read_slot] ||
        pipe_slot_owner[write_slot]) return -EINVAL;
    unsigned index = NAS_PIPE_COUNT;
    for (unsigned i = 0; i < NAS_PIPE_COUNT; i++)
        if (!pipe_table[i].used) { index = i; break; }
    if (index == NAS_PIPE_COUNT) return -ENFILE;
    struct nas_pipe *pipe = &pipe_table[index];
    pipe->used = true;
    pipe->head = pipe->count = 0;
    pipe->read_slot = read_slot;
    pipe->write_slot = write_slot;
    pipe_slot_owner[read_slot] = (uint8_t)(index + 1);
    pipe_slot_owner[write_slot] = (uint8_t)(index + 1);
    return 0;
}
void nas_pipe_release_description(unsigned slot) {
    struct nas_pipe *pipe = pipe_for_slot(slot);
    if (!pipe) return;
    /* Forget the endpoint BEFORE the global slot is reused. A new FD
     * referencing this same slot must never revive an old pipe's writer.
     */
    if (slot == pipe->read_slot) pipe->read_slot = NAS_FD_LIMIT;
    if (slot == pipe->write_slot) pipe->write_slot = NAS_FD_LIMIT;
    pipe_slot_owner[slot] = 0;
    if (!pipe_has_reader(pipe) && !pipe_has_writer(pipe)) {
        pipe->used = false;
        pipe->count = pipe->head = 0;
    }
}
bool nas_pipe_read_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    return !pipe || pipe->count || !pipe_has_writer(pipe);
}
bool nas_pipe_write_ready(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    /* Waking for EPIPE is mandatory, not a false claim of writability. */
    return !pipe || pipe->count < NAS_PIPE_CAP || !pipe_has_reader(pipe);
}
bool nas_pipe_poll_readable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    return pipe && (pipe->count || !pipe_has_writer(pipe));
}
bool nas_pipe_poll_writable(unsigned slot) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    return pipe && pipe_has_reader(pipe) && pipe->count < NAS_PIPE_CAP;
}
int64_t nas_pipe_read_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    if (!pipe) return -EBADF;
    if (!requested) return 0;
    if (!pipe->count)
        return !pipe_has_writer(pipe) ? 0 :
            nonblock ? -EAGAIN : -EOPNOTSUPP;
    if (requested > pipe->count) requested = pipe->count;
    return (int64_t)requested;
}
uint8_t nas_pipe_read_byte(unsigned slot, unsigned offset) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
    return pipe ? pipe->bytes[(pipe->head + offset) % NAS_PIPE_CAP] : 0;
}
void nas_pipe_read_commit(unsigned slot, unsigned count) {
    struct nas_pipe *pipe = pipe_for_slot(slot);
    if (!pipe) return;
    pipe->head = (pipe->head + count) % NAS_PIPE_CAP;
    pipe->count -= count;
}
int64_t nas_pipe_write_amount(unsigned slot, uint64_t requested, bool nonblock) {
    const struct nas_pipe *pipe = pipe_for_slot(slot);
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
    struct nas_pipe *pipe = pipe_for_slot(slot);
    if (!pipe) return;
    pipe->bytes[(pipe->head + pipe->count + offset) % NAS_PIPE_CAP] = value;
}
void nas_pipe_write_commit(unsigned slot, unsigned count) {
    struct nas_pipe *pipe = pipe_for_slot(slot);
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
