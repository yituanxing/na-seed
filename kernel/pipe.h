#ifndef NASEED_PIPE_H
#define NASEED_PIPE_H

#include <stdbool.h>
#include <stdint.h>

/* Bounded anonymous-pipe backend. Only shared open-description slots enter
 * this API; syscall/FD decoding, user copying, SIGPIPE and task scheduling
 * stay in the callers. Two endpoint slots have independent refcounts.
 */
#define NAS_PIPE_COUNT 32
#define NAS_PIPE_CAP 4096U

/* Bind two already-reserved open-description slots; no user addresses. */
int nas_pipe_create(unsigned read_slot, unsigned write_slot);
/* A Unix stream socketpair is two opposing pipe channels with the same two
 * refcounted open descriptions. Both endpoints support read and write. */
int nas_pipe_create_socketpair(unsigned first_slot, unsigned second_slot);
void nas_pipe_release_description(unsigned slot);
/* Return the opposing receive-description of a connected AF_UNIX stream.
 * Used for genuine SCM_RIGHTS delivery, never a process-local FD number. */
int nas_pipe_peer_slot(unsigned writing_slot);

/* Readiness reports also include EOF (read side) and the EPIPE condition
 * (write side) so a blocked syscall can resume to return the real result.
 */
bool nas_pipe_read_ready(unsigned slot);
bool nas_pipe_write_ready(unsigned slot);
bool nas_pipe_poll_readable(unsigned slot);
bool nas_pipe_poll_writable(unsigned slot);

/* Preflight returns available/accepted byte count or negative errno.
 * Caller owns user-memory validation and transfers bytes before commit.
 * No task can interleave on a single-core syscall boundary until commit.
 */
int64_t nas_pipe_read_amount(unsigned slot, uint64_t requested, bool nonblock);
uint8_t nas_pipe_read_byte(unsigned slot, unsigned offset);
void nas_pipe_read_commit(unsigned slot, unsigned count);
int64_t nas_pipe_write_amount(unsigned slot, uint64_t requested, bool nonblock);
void nas_pipe_write_byte(unsigned slot, unsigned offset, uint8_t value);
void nas_pipe_write_commit(unsigned slot, unsigned count);

/* Read-only diagnostic snapshot, not a public pipe object. */
bool nas_pipe_snapshot(unsigned index, unsigned *bytes,
                       bool *readers, bool *writers);
#endif
