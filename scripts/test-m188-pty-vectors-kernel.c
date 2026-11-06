/* M188: actual NaSeed PTY + N_TTY + FileOps + usercopy. Only MM, Task
 * identity, monotonic clock and the peer terminal process are host fixtures.
 * M187's native three-ISA blocked-first-read remains a separate guest gate. */
#define main m106_fixture_main
#define NAS_TEST_REAL_PTY 1
#define NAS_TEST_OVERRIDE_MONOTONIC 1
#include "test-m106-usercopy-syscalls.c"
#undef main

static uint64_t simulated_ns;
uint64_t nas_arch_monotonic_ns(void) { return simulated_ns; }
struct nas_task *nas_task_at(unsigned slot) {(void)slot;return NULL;}
static bool test_vt_controlling;
bool nas_vt_controlling(void) {return test_vt_controlling;}

enum { MASTER_FD=51,SLAVE_FD=52,MASTER_SLOT=55,SLAVE_SLOT=56 };
static const uint64_t t_addr=TEST_BASE+3*TEST_PAGE+40;
static const uint64_t vector_addr=TEST_BASE;
static const uint64_t first_addr=TEST_BASE+TEST_PAGE+11;
static const uint64_t second_addr=TEST_BASE+2*TEST_PAGE+13;
static struct nas_termios termios_get(void) {
    struct nas_termios t={0};
    assert(nas_pty_ioctl(SLAVE_FD,0x5401,t_addr)==0);
    memcpy(&t,user_ptr(t_addr),sizeof(t));
    return t;
}
static void termios_mode(bool canonical,unsigned vmin,unsigned vtime) {
    struct nas_termios t=termios_get();
    t.lflag&=~(0000010U|0000001U|0000002U); /* ECHO,ISIG,ICANON */
    if(canonical)t.lflag|=0000002U;
    t.cc[6]=(uint8_t)vmin;
    t.cc[5]=(uint8_t)vtime;
    memcpy(user_ptr(t_addr),&t,sizeof(t));
    assert(nas_pty_ioctl(SLAVE_FD,0x5404,t_addr)==0);
    guest_task.wait.deadline_active=false;
    guest_task.wait.tty_observed_bytes=0;
    simulated_ns=0;
    nas_open_files[SLAVE_SLOT].status_flags=0;
}
static void produce(const char *str,unsigned count) {
    assert(nas_pty_write(MASTER_SLOT,(const uint8_t *)str,count)==count);
}
static void set_vector(unsigned left,unsigned right) {
    a008_iov(vector_addr,0,first_addr,left);
    a008_iov(vector_addr,1,second_addr,right);
}
static void begin_wait(unsigned requested) {
    guest_task.wait.tty_requested_bytes=requested;
    nas_pty_read_wait_start(SLAVE_SLOT,&guest_task,requested);
}
static bool wait_ready(unsigned requested) {
    return nas_pty_read_wait_ready(SLAVE_SLOT,&guest_task,requested);
}
static void read_one(char expected) {
    assert(nas_fs_fd_read(SLAVE_FD,first_addr,1)==1);
    assert(*user_ptr(first_addr)==expected);
}
static void verify_session_leader_open(void) {
    /* sshpass's child does setsid(); open(pts,O_RDWR); close(pts); then
     * opens /dev/tty from the exec'd program.  No explicit TIOCSCTTY is
     * issued.  Exercise the production PTY owner, not a fabricated tty. */
    assert(!nas_pty_controlling());
    nas_pty_open_session(SLAVE_SLOT,0x100U); /* O_NOCTTY */
    assert(!nas_pty_controlling());
    guest_task.pid++;
    nas_pty_open_session(SLAVE_SLOT,0); /* not a session leader */
    assert(!nas_pty_controlling());
    guest_task.pid--;
    nas_pty_open_session(MASTER_SLOT,0); /* master never claims */
    assert(!nas_pty_controlling());
    bootstrap_tty_present=true;
    nas_pty_open_session(SLAVE_SLOT,0); /* existing serial controlling tty */
    assert(!nas_pty_controlling());
    bootstrap_tty_present=false;
    test_vt_controlling=true;
    nas_pty_open_session(SLAVE_SLOT,0); /* existing virtual console */
    assert(!nas_pty_controlling());
    test_vt_controlling=false;

    nas_pty_open_session(SLAVE_SLOT,0);
    assert(nas_pty_controlling());
    nas_pty_release(SLAVE_SLOT); /* sshpass closes the claiming slave */
    assert(nas_pty_controlling());
    assert(nas_pty_attach_controlling(SLAVE_SLOT)==0);
    assert(nas_pty_attach_controlling(57)==0); /* /dev/tty after close */
    nas_pty_release(57);
    guest_task.pid=guest_task.sid=42;
    assert(!nas_pty_controlling());
    nas_pty_open_session(SLAVE_SLOT,0); /* cannot steal another SID's PTY */
    assert(!nas_pty_controlling());
    assert(nas_pty_attach_controlling(57)==-ENXIO);
    guest_task.pid=guest_task.sid=41;
    assert(nas_pty_controlling());
    puts("[M205 PTY SESSION OPEN PASS] setsid/auto-CTTY, O_NOCTTY, leader, nonsteal, /dev/tty");
}
static void verify_matrix(void) {
    /* Linux N_TTY 0/0: an empty blocking read returns 0 immediately,
     * while poll does not advertise a fictional readable character. */
    termios_mode(false,0,0);
    assert(nas_fs_fd_read(SLAVE_FD,first_addr,1)==0);
    assert(!(nas_pty_poll(SLAVE_SLOT,1U)&1U));
    produce("A",1);assert(nas_pty_poll(SLAVE_SLOT,1U)&1U);
    read_one('A');

    /* 0/1: the deadline begins with the first blocked read, not with
     * the first input byte. No bytes by 100ms => genuine zero-length. */
    termios_mode(false,0,1);
    assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);
    begin_wait(1);assert(guest_task.wait.deadline_active);
    simulated_ns=99999999ULL;assert(!wait_ready(1));
    simulated_ns=100000000ULL;assert(wait_ready(1));
    assert(nas_fs_fd_read(SLAVE_FD,first_addr,1)==0);
    termios_mode(false,0,1);
    assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);
    begin_wait(1);produce("B",1);
    assert(wait_ready(1));read_one('B');

    for(unsigned time_mode=0;time_mode<2;time_mode++) {
        /* 1/0 and 1/1: empty reads wait; first byte admits return. */
        termios_mode(false,1,time_mode);
        assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);
        begin_wait(1);
        assert(!guest_task.wait.deadline_active);
        produce("C",1);
        assert(wait_ready(1));read_one('C');

        /* 2/0 and 2/1: the whole logical 2-byte request must satisfy
         * VMIN=2 before readv splits into [iov1=1, iov2=1]. */
        termios_mode(false,2,time_mode);
        set_vector(1,1);
        assert((int64_t)nas_fs_fd_readv(SLAVE_FD,vector_addr,2)==-EAGAIN);
        begin_wait(2);
        produce("D",1);
        assert(!wait_ready(2));
        assert(!(nas_pty_poll(SLAVE_SLOT,1U)&1U));
        if(time_mode) {
            assert(guest_task.wait.deadline_active);
            simulated_ns=50000000ULL;
            assert(!wait_ready(2));
            simulated_ns=100000000ULL;
            assert(wait_ready(2));
            assert(nas_fs_fd_readv(SLAVE_FD,vector_addr,2)==1);
            assert(*user_ptr(first_addr)=='D');
        } else {
            assert(!guest_task.wait.deadline_active);
            produce("E",1);
            assert(wait_ready(2));
            assert(nas_pty_poll(SLAVE_SLOT,1U)&1U);
            assert(nas_fs_fd_readv(SLAVE_FD,vector_addr,2)==2);
            assert(*user_ptr(first_addr)=='D');
            assert(*user_ptr(second_addr)=='E');
        }
    }
    puts("[M188 PTY VMIN/VTIME MATRIX PASS] raw 0/1/2 x 0/1, poll and readv [1,1]");
}
static void verify_nonblock_and_races(void) {
    termios_mode(false,2,1);
    nas_open_files[SLAVE_SLOT].status_flags=0x800U;
    assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);
    set_vector(1,1);
    produce("F",1);
    assert(nas_fs_fd_readv(SLAVE_FD,vector_addr,2)==1);
    assert(*user_ptr(first_addr)=='F');
    assert(!(nas_pty_poll(SLAVE_SLOT,1U)&1U));
    nas_open_files[SLAVE_SLOT].status_flags=0;

    /* Competing reads must disarm an orphaned interbyte deadline when
     * another Task drained the only byte before this Task was resumed. */
    produce("G",1);begin_wait(2);
    assert(guest_task.wait.deadline_active);
    read_one('G'); /* a peer Task would use the same underlying PTY */
    simulated_ns=100000000ULL;
    assert(!wait_ready(2));
    assert(!guest_task.wait.deadline_active);
    produce("H",1);
    assert(!wait_ready(2));
    assert(guest_task.wait.deadline_active);
    simulated_ns=200000000ULL;
    assert(wait_ready(2));
    read_one('H');
    puts("[M188 PTY NONBLOCK/RACE PASS] O_NONBLOCK, cleared stale timer, restarted interbyte");
}
static void verify_canon_and_winch(void) {
    termios_mode(true,1,0);
    produce("X",1);
    assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);
    produce("\n",1);
    set_vector(1,1);
    assert(nas_fs_fd_readv(SLAVE_FD,vector_addr,2)==2);
    assert(*user_ptr(first_addr)=='X' && *user_ptr(second_addr)=='\n');
    produce("\004",1);
    assert(nas_fs_fd_read(SLAVE_FD,first_addr,1)==0);
    assert((int64_t)nas_fs_fd_read(SLAVE_FD,first_addr,1)==-EAGAIN);

    assert(nas_pty_ioctl(SLAVE_FD,0x540e,0)==0); /* foreground session */
    const uint16_t first_size[4]={36,120,0,0};
    const uint16_t second_size[4]={42,140,0,0};
    memcpy(user_ptr(t_addr),first_size,sizeof(first_size));
    assert(nas_pty_ioctl(MASTER_FD,0x5414,t_addr)==0);
    uint64_t sid=0,pgid=0;
    assert(nas_pty_take_signal(&sid,&pgid)==28U);
    assert(sid==guest_task.sid && pgid==guest_task.pgid);
    assert(nas_pty_ioctl(SLAVE_FD,0x5414,t_addr)==0); /* unchanged */
    assert(nas_pty_take_signal(&sid,&pgid)==0);
    memcpy(user_ptr(t_addr),second_size,sizeof(second_size));
    for(unsigned i=0;i<64;i++) {
        /* Each changed dimension posts one event, preserving FIFO order. */
        uint16_t size[4]={ (uint16_t)(50+i),140,0,0 };
        memcpy(user_ptr(t_addr),size,sizeof(size));
        assert(nas_pty_ioctl(SLAVE_FD,0x5414,t_addr)==0);
    }
    uint16_t blocked_size[4]={222,200,0,0};
    memcpy(user_ptr(t_addr),blocked_size,sizeof(blocked_size));
    assert((int64_t)nas_pty_ioctl(SLAVE_FD,0x5414,t_addr)==-EAGAIN);
    assert(nas_pty_ioctl(SLAVE_FD,0x5413,t_addr)==0);
    uint16_t saved_size[4]={0};
    memcpy(saved_size,user_ptr(t_addr),sizeof(saved_size));
    assert(saved_size[0]==113 && saved_size[1]==140);
    for(unsigned i=0;i<64;i++)
        assert(nas_pty_take_signal(&sid,&pgid)==28U);
    assert(nas_pty_take_signal(&sid,&pgid)==0);
    memcpy(user_ptr(t_addr),blocked_size,sizeof(blocked_size));
    assert(nas_pty_ioctl(SLAVE_FD,0x5414,t_addr)==0);
    assert(nas_pty_take_signal(&sid,&pgid)==28U);
    puts("[M188 PTY CANON/WINCH PASS] EOF token, aggregate canonical readv, changed-size SIGWINCH FIFO and backpressure");
}
static void verify_hangup(void) {
    termios_mode(false,2,0);
    produce("Q",1);
    begin_wait(2);
    assert(!wait_ready(2));
    nas_pty_release(MASTER_SLOT);
    assert(wait_ready(2));
    assert((nas_pty_poll(SLAVE_SLOT,1U)&0x19U)==0x19U);
    read_one('Q');
    assert(nas_fs_fd_read(SLAVE_FD,first_addr,1)==0);
    puts("[M188 PTY HANGUP PASS] pending VMIN2 returns short buffered tail and then EOF");
}
int main(void) {
    guest_task.mm=&guest_mm;
    guest_files.refs=1;
    guest_task.files=&guest_files;
    guest_task.fds=&guest_files.table;
    guest_task.fs=&guest_fs;
    nas_fd_system_init();
    reset_mapping();
    guest_task.pid=guest_task.sid=guest_task.pgid=41;
    guest_task.fds->entries[MASTER_FD]=(struct nas_fd_entry){
        .kind=FD_PTY_MASTER,.slot=MASTER_SLOT};
    guest_task.fds->entries[SLAVE_FD]=(struct nas_fd_entry){
        .kind=FD_PTY_SLAVE,.slot=SLAVE_SLOT};
    nas_open_files[MASTER_SLOT]=(struct nas_open_file){.refcount=1,.ops=&ops_pty};
    nas_open_files[SLAVE_SLOT]=(struct nas_open_file){.refcount=1,.ops=&ops_pty};
    assert(nas_pty_new(MASTER_SLOT)==0);
    uint32_t unlocked=0;
    memcpy(user_ptr(t_addr),&unlocked,sizeof(unlocked));
    assert(nas_pty_ioctl(MASTER_FD,0x40045431,t_addr)==0);
    assert(nas_pty_attach(0,SLAVE_SLOT)==0);
    assert(ops_pty.aggregate_readv && ops_pty.read_finalize_zero &&
           ops_pty.read_wait_start && ops_pty.read_wait_ready);
    verify_session_leader_open();
    verify_matrix();
    verify_nonblock_and_races();
    verify_canon_and_winch();
    verify_hangup();
    puts("[M188 PRODUCTION PTY FILEOPS PASS] kernel PTY + ldisc + real FileOps/readv + usercopy");
    return 0;
}
