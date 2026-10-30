/* Compile the REAL NaSeed FileOps, usercopy, TTY and N_TTY, keeping only
 * CPU RX, user MM, Task identity and monotonic clock as host fixtures.
 * Reuse M106's verified production FD/usercopy/heap harness. */
#define main m106_fixture_main
#include "test-m106-usercopy-syscalls.c"
#undef main
#include "../kernel/drivers/tty.h"
#include "../kernel/task/wait.h"

static const uint8_t *rx_bytes;
static unsigned rx_length;
static uint64_t fake_now;
bool nas_arch_console_has_rx(void) { return true; }
bool nas_arch_console_getc(uint8_t *dst) {
    if(!rx_length)return false;
    *dst=*rx_bytes++;rx_length--;return true;
}
uint64_t nas_arch_monotonic_ns(void) { return fake_now; }
struct nas_task *nas_task_at(unsigned slot) { (void)slot; return NULL; }
static void receive(const char *bytes,unsigned count) {
    assert(!rx_length);
    rx_bytes=(const uint8_t *)bytes;
    rx_length=count;
}
static void tty_set(bool canonical,unsigned vmin,unsigned vtime) {
    const uint64_t addr=TEST_BASE+3*TEST_PAGE+40;
    assert(nas_tty_ioctl(0x5401,addr)==0);
    struct nas_termios t;
    memcpy(&t,user_ptr(addr),sizeof(t));
    t.lflag &= ~(0000010U|0000001U|0000002U); /* no ECHO/ISIG/ICANON */
    if(canonical)t.lflag |= 0000002U;
    t.cc[6]=(uint8_t)vmin;t.cc[5]=(uint8_t)vtime;
    memcpy(user_ptr(addr),&t,sizeof(t));
    assert(nas_tty_ioctl(0x5404,addr)==0); /* flush old pending bytes */
    guest_task.wait.deadline_active=false;
    guest_task.wait.tty_observed_bytes=0;
}
int main(void) {
    assert(m106_fixture_main()==0);
    reset_mapping();
    const unsigned fd=50,slot=55;
    assert(guest_task.fds->entries[fd].kind==FD_CLOSED);
    assert(!nas_open_files[slot].refcount);
    guest_task.pid=guest_task.sid=guest_task.pgid=41;
    nas_tty_init();
    assert(nas_tty_ioctl(0x540e,0)==0);
    guest_task.fds->entries[fd]=(struct nas_fd_entry){.kind=FD_TTY,.slot=slot};
    nas_open_files[slot]=(struct nas_open_file){.refcount=1,.ops=&ops_tty};
    assert(ops_tty.read_plan && ops_tty.aggregate_readv &&
           ops_tty.read_finalize_zero);
    const uint64_t iov=TEST_BASE;
    const uint64_t first=TEST_BASE+TEST_PAGE+11;
    const uint64_t second=TEST_BASE+2*TEST_PAGE+13;
    a008_iov(iov,0,first,1);
    a008_iov(iov,1,second,1);

    /* The original 2-byte readv is admitted once, not as two read(1)s. */
    tty_set(false,2,0);
    receive("A",1);
    uint64_t requested=0;
    assert((int64_t)nas_fs_fd_readv_with_size(fd,iov,2,&requested)==-EAGAIN);
    assert(requested==2);
    assert(!nas_tty_read_ready(&guest_task,requested));
    receive("B",1);
    assert(nas_tty_read_ready(&guest_task,requested));
    assert(nas_fs_fd_readv(fd,iov,2)==2);
    assert(*user_ptr(first)=='A' && *user_ptr(second)=='B');

    /* Linux readv preserves a successful valid-prefix copy when a later
     * destination iovec is within USER_LIMIT but unmapped. */
    reset_mapping();
    tty_set(false,2,0);
    a008_iov(iov,0,first,1);a008_iov(iov,1,second,1);
    receive("CD",2);
    present[2]=false;
    assert(nas_fs_fd_readv(fd,iov,2)==1);
    assert(*user_ptr(first)=='C');
    present[2]=true;
    assert(nas_fs_fd_read(fd,second,1)==1);
    assert(*user_ptr(second)=='D');

    /* Same partial-progress rule when one iovec crosses a mapped page and
     * a missing page: validate only the writable prefix before consuming. */
    reset_mapping();
    tty_set(false,2,0);
    a008_iov(iov,0,TEST_BASE+2*TEST_PAGE-1,2);
    receive("EF",2);
    present[2]=false;
    assert(nas_fs_fd_readv(fd,iov,1)==1);
    assert(pages[1][TEST_PAGE-1]=='E');
    present[2]=true;
    assert(nas_fs_fd_read(fd,second,1)==1);
    assert(*user_ptr(second)=='F');

    /* One canonical EOF token must be consumed exactly once when read_plan
     * returns a legitimate zero-byte result. */
    reset_mapping();
    tty_set(true,1,0);
    a008_iov(iov,0,first,1);
    receive("\004",1); /* default VEOF */
    assert(nas_fs_fd_readv(fd,iov,1)==0);
    assert((int64_t)nas_fs_fd_readv(fd,iov,1)==-EAGAIN);

    /* VMIN=0,VTIME>0 uses a Task-owned deadline; budget zero after timeout
     * is an ordinary empty result, never a second EOF side effect. */
    reset_mapping();
    tty_set(false,0,1);
    a008_iov(iov,0,first,1);
    fake_now=0;
    assert((int64_t)nas_fs_fd_readv(fd,iov,1)==-EAGAIN);
    nas_tty_blocking_read_start(&guest_task);
    assert(guest_task.wait.deadline_active);
    fake_now=100000000ULL;
    assert(nas_fs_fd_readv(fd,iov,1)==0);

    /* Nonblocking raw TTY ignores VMIN if at least one byte is available. */
    reset_mapping();
    tty_set(false,2,0);
    a008_iov(iov,0,first,1);
    nas_open_files[slot].status_flags=0x800ULL;
    receive("G",1);
    assert(nas_fs_fd_readv(fd,iov,1)==1);
    assert(*user_ptr(first)=='G');
    /* Existing fork/exec/close completion paths call this shared by-value
     * Task wait reset, which must retire the per-request byte hint too. */
    struct nas_task parked={0};
    parked.wait.tty_requested_bytes=2048;
    parked.wait.tty_observed_bytes=3;
    nas_task_wait_reset(&parked);
    assert(!parked.wait.tty_requested_bytes &&
           !parked.wait.tty_observed_bytes && !parked.wait.pending_valid);
    puts("[NAS-A013 NASEED TTY PASS] VMIN readv logical total, partial destination, canonical EOF, VTIME, O_NONBLOCK");
    return 0;
}
