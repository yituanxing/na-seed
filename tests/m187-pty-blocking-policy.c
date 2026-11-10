#include <assert.h>
#include <stdio.h>
#include "../kernel/fs/file_ops.h"

int main(void) {
    const uint8_t pipe_socket=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY;
    const uint8_t pty=NAS_IO_WAIT_INITIAL_EAGAIN|NAS_IO_WAIT_RETRY;

    /* Legacy pipe/socket initial admission remains EOPNOTSUPP-only. */
    assert(nas_io_wait_policy(pipe_socket,false,-EOPNOTSUPP,true));
    assert(!nas_io_wait_policy(pipe_socket,false,-EAGAIN,true));
    assert(nas_io_wait_policy(pipe_socket,false,-EAGAIN,false));
    assert(nas_io_wait_policy(pipe_socket,false,-EOPNOTSUPP,false));

    /* Real ncurses/cbreak read(0) waits for PTY master keyboard input. */
    assert(nas_io_wait_policy(pty,false,-EAGAIN,true));
    assert(nas_io_wait_policy(pty,false,-EAGAIN,false));
    assert(!nas_io_wait_policy(pty,false,-EOPNOTSUPP,true));
    assert(!nas_io_wait_policy(pty,false,-EIO,true));

    /* O_NONBLOCK never parks either family on first attempt or replay. */
    for(int initial=0;initial<=1;initial++) {
        assert(!nas_io_wait_policy(pty,true,-EAGAIN,initial));
        assert(!nas_io_wait_policy(pipe_socket,true,-EOPNOTSUPP,initial));
    }
    puts("[M187 PTY BLOCKING POLICY PASS] real raw PTY EAGAIN parks; "
         "pipe/socket admission and O_NONBLOCK unchanged");
}
