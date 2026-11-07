/* Host execution of NaSeed's actual pipe ring, heap and OpenFile lifetime.
 * This is not a mocked fcntl oracle: the dispatch ABI is separately compiled
 * on x86-64, AArch64 and RV64 by check-three-isa-core.py.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/errno.h"
#include "../kernel/fd.h"
#include "../kernel/pipe.h"

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
void nas_print(const char *message) { (void)message; }
void nas_dec(uint64_t value) { (void)value; }

static void write_pattern(unsigned slot, unsigned count, unsigned seed) {
    assert(nas_pipe_write_amount(slot,count,true)==(int64_t)count);
    for (unsigned i=0;i<count;i++)
        nas_pipe_write_byte(slot,i,(uint8_t)((seed+i)%251));
    nas_pipe_write_commit(slot,count);
}
static void read_pattern(unsigned slot, unsigned count, unsigned seed) {
    assert(nas_pipe_read_amount(slot,count,true)==(int64_t)count);
    for (unsigned i=0;i<count;i++)
        assert(nas_pipe_read_byte(slot,i)==(uint8_t)((seed+i)%251));
    nas_pipe_read_commit(slot,count);
}
static void check_pipe_capacity(void) {
    nas_open_files[10].refcount=2; /* two dup/fork aliases of read endpoint */
    nas_open_files[11].refcount=1;
    assert(nas_pipe_create(10,11)==0);
    assert(nas_pipe_capacity(10)==NAS_PIPE_CAP);
    assert(nas_pipe_capacity(11)==NAS_PIPE_CAP);
    assert(nas_pipe_capacity(12)==-EBADF);
    assert(nas_pipe_set_capacity(11,-1)==-EINVAL);
    assert(nas_pipe_set_capacity(11,0)==NAS_PIPE_CAP);
    assert(nas_pipe_set_capacity(10,2500)==NAS_PIPE_CAP);
    /* The 1 MiB request cannot fit in the 1 MiB bootstrap arena once heap
     * metadata is counted. Without a grow provider this is transactional. */
    assert(nas_pipe_set_capacity(11,NAS_PIPE_MAX_CAP)==-ENOMEM);
    assert(nas_pipe_capacity(10)==NAS_PIPE_CAP);

    write_pattern(11,3000,0);
    read_pattern(10,2000,0);
    write_pattern(11,3000,3000); /* wrap around 4 KiB original ring */
    assert(nas_pipe_set_capacity(10,5000)==8192);
    assert(nas_pipe_capacity(11)==8192);
    assert(nas_pipe_set_capacity(10,NAS_PIPE_MAX_CAP+1)==-EPERM);
    read_pattern(10,1000,2000);
    read_pattern(10,3000,3000); /* growth preserved wrapped FIFO bytes */

    write_pattern(11,7000,6010);
    assert(nas_pipe_set_capacity(11,4096)==-EBUSY);
    assert(nas_pipe_capacity(10)==8192);
    read_pattern(10,3000,6010);
    assert(nas_pipe_set_capacity(10,1)==4096);
    assert(nas_pipe_capacity(11)==4096);
    read_pattern(10,4000,9010); /* shrink preserved all remaining data */

    /* PIPE_BUF stays 4 KiB even after ring growth to 8 KiB. */
    assert(nas_pipe_set_capacity(11,7000)==8192);
    write_pattern(11,8092,10010);
    assert(nas_pipe_poll_writable(11));
    assert(nas_pipe_write_amount(11,101,true)==-EAGAIN);
    assert(nas_pipe_write_amount(11,4096,true)==-EAGAIN);
    assert(nas_pipe_write_amount(11,4097,true)==100);
    for (unsigned i=0;i<100;i++)
        nas_pipe_write_byte(11,i,(uint8_t)((18102U+i)%251));
    nas_pipe_write_commit(11,100);
    assert(!nas_pipe_poll_writable(11));
    assert(nas_pipe_write_amount(11,1,true)==-EAGAIN);
    read_pattern(10,8092,10010);
    read_pattern(10,100,18102);
    assert(nas_pipe_write_ready(11));

    /* Dup/Fork closes one alias but the OpenFile endpoint survives. */
    nas_open_files[10].refcount=1;
    assert(nas_pipe_capacity(10)==8192);
    nas_open_files[10].refcount=0;
    nas_pipe_release_description(10);
    assert(nas_pipe_write_amount(11,1,true)==-EPIPE);
    nas_open_files[11].refcount=0;
    nas_pipe_release_description(11);
    assert(nas_pipe_capacity(11)==-EBADF);

    /* Recycled pipe slot must not retain the previous heap-backed capacity. */
    nas_open_files[10].refcount=nas_open_files[11].refcount=1;
    assert(nas_pipe_create(10,11)==0);
    assert(nas_pipe_capacity(10)==NAS_PIPE_CAP);
    write_pattern(11,256,42);
    read_pattern(10,256,42);
    nas_open_files[10].refcount=nas_open_files[11].refcount=0;
    nas_pipe_release_description(10);
    nas_pipe_release_description(11);
}
static void check_socketpair_stream(void) {
    nas_open_files[30].refcount=nas_open_files[31].refcount=1;
    assert(nas_pipe_create_socketpair(30,31)==0);
    assert(nas_pipe_capacity(30)==-EBADF);
    assert(nas_pipe_capacity(31)==-EBADF);
    assert(nas_pipe_set_capacity(30,8192)==-EBADF);
    assert(nas_socket_write_amount(30,5000,true)==NAS_PIPE_CAP);
    write_pattern(30,4000,17);
    /* Unlike pipe writes, stream writes can make short progress <= PIPE_BUF. */
    assert(nas_socket_write_amount(30,4096,true)==96);
    read_pattern(31,4000,17);
    assert(nas_pipe_socket_shutdown(30,1)==0);
    assert(nas_pipe_read_amount(31,1,true)==0);
    nas_open_files[30].refcount=nas_open_files[31].refcount=0;
    nas_pipe_release_description(30);
    nas_pipe_release_description(31);
}
int main(void) {
    check_pipe_capacity();
    check_socketpair_stream();
    puts("[PIPE CAPACITY HOST PASS] ring grow/shrink, wrapped FIFO, pipe atomicity, socket stream, OpenFile lifetime");
    return 0;
}
