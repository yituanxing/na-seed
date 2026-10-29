/* Host unit test against the REAL NaSeed PTY + line discipline sources.
 * Only Task/FD and user-address translation are mocked; queue/bind/termios
 * behavior is not reimplemented here. This is NOT a guest QEMU proof. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../kernel/errno.h"
#include "../kernel/task.h"
#include "../kernel/drivers/pty.h"
#include "../kernel/drivers/tty.h"

static struct nas_task owner={.pid=42,.sid=42,.pgid=77};
static struct nas_fd_table fds;
struct nas_task *nas_current_task=&owner;
struct nas_task *nas_task_at(unsigned slot) {(void)slot;return NULL;}
bool nas_user_read_bytes(uint64_t address,uint8_t *dst,uint64_t length) {
    if(!address && length)return false;
    memcpy(dst,(const void *)(uintptr_t)address,(size_t)length);
    return true;
}
bool nas_user_write_bytes(uint64_t address,const uint8_t *src,uint64_t length) {
    if(!address && length)return false;
    memcpy((void *)(uintptr_t)address,src,(size_t)length);
    return true;
}
static void push(unsigned fd,uint8_t b) {
    assert(nas_pty_write(fd,(uint64_t)(uintptr_t)&b,1)==1);
}
static void take(unsigned signum,uint64_t pgid) {
    uint64_t sid=0,group=0;
    assert(nas_pty_take_signal(&sid,&group)==signum);
    assert(sid==42 && group==pgid);
}
static void empty(void) {
    uint64_t sid=111,pgid=222;
    assert(nas_pty_take_signal(&sid,&pgid)==0);
    assert(sid==111 && pgid==222);
}
static struct nas_termios get_termios(unsigned fd) {
    struct nas_termios t={0};
    assert(nas_pty_ioctl(fd,0x5401,(uint64_t)(uintptr_t)&t)==0);
    return t;
}
static void set_termios(unsigned fd,struct nas_termios *t) {
    assert(nas_pty_ioctl(fd,0x5402,(uint64_t)(uintptr_t)t)==0);
}
static void make_pair(unsigned master,unsigned slave,unsigned number) {
    fds.entries[master].slot=(uint8_t)master;
    fds.entries[slave].slot=(uint8_t)slave;
    assert(nas_pty_new(master)==0);
    uint32_t unlocked=0;
    assert(nas_pty_ioctl(master,0x40045431,
                         (uint64_t)(uintptr_t)&unlocked)==0);
    assert(nas_pty_attach(number,slave)==0);
    assert(nas_pty_ioctl(slave,0x540e,0)==0); /* TIOCSCTTY */
}
int main(void) {
    owner.fds=&fds;
    make_pair(0,1,0);
    struct nas_termios t=get_termios(1);
    assert(t.lflag&1U); /* real ISIG default */
    push(0,3);take(2,77); /* canonical VINTR */
    push(0,28);take(3,77); /* canonical VQUIT */
    push(0,26);take(20,77); /* canonical VSUSP */

    t.lflag&=~2U; /* ICANON off, ISIG remains on */
    set_termios(1,&t);
    push(0,3);take(2,77);
    t.cc[0]='X';set_termios(1,&t);
    push(0,'X');take(2,77);

    t.cc[0]='\n';t.iflag|=0000400; /* ICRNL */
    set_termios(1,&t);
    push(0,'\r'); /* Linux compares VINTR before CR->NL translation. */
    empty();
    uint8_t bytes[2]={0};
    assert(nas_pty_read(1,(uint64_t)(uintptr_t)bytes,1)==1);
    assert(bytes[0]=='\n');
    t.cc[0]='\r';set_termios(1,&t);
    push(0,'\r');take(2,77);

    t.lflag&=~1U; /* ISIG off means Ctrl-C becomes ordinary data. */
    set_termios(1,&t);
    push(0,3);
    assert(nas_pty_read(1,(uint64_t)(uintptr_t)bytes,1)==1);
    assert(bytes[0]==3);
    empty();

    /* The production PTY must retain both input representations with
     * NOFLSH, and discard both on an ordinary Ctrl-C. The Linux oracle
     * above independently proves the raw NOFLSH observable behavior. */
    t.lflag=(t.lflag|1U|0000200U)&~2U; /* ISIG | NOFLSH, !ICANON */
    t.cc[0]=3;
    set_termios(1,&t);
    push(0,'A');push(0,3);take(2,77);
    assert(nas_pty_read(1,(uint64_t)(uintptr_t)bytes,1)==1);
    assert(bytes[0]=='A');
    t.lflag&=~0000200U; /* default: signal flushes unread raw data */
    set_termios(1,&t);
    push(0,'B');push(0,3);take(2,77);
    assert((int64_t)nas_pty_read(1,(uint64_t)(uintptr_t)bytes,1)==-EAGAIN);

    t.lflag|=2U|0000200U; /* canonical, ISIG, NOFLSH */
    set_termios(1,&t);
    push(0,'C');push(0,3);take(2,77);
    push(0,'\n');
    assert(nas_pty_read(1,(uint64_t)(uintptr_t)bytes,2)==2);
    assert(bytes[0]=='C' && bytes[1]=='\n');
    t.lflag&=~0000200U; /* canonical default flushes unfinished line */
    set_termios(1,&t);
    push(0,'D');push(0,3);take(2,77);
    push(0,'\n');
    assert(nas_pty_read(1,(uint64_t)(uintptr_t)bytes,2)==1);
    assert(bytes[0]=='\n');
    empty();

    /* Multiple PTYs snapshot their own foreground group, no global PGID. */
    owner.pgid=88;
    make_pair(2,3,1);
    struct nas_termios other=get_termios(3);
    push(0,'\r'); /* ISIG remains disabled on PTY0. */
    push(2,3);take(2,88);
    owner.pgid=77;
    t.lflag|=1U;t.cc[0]=3;set_termios(1,&t);
    push(0,3);push(2,28);
    take(2,77);take(3,88);
    empty();

    /* Bound the queue without silent overwrite or losing ordering. */
    for(unsigned i=0;i<64;i++)push(0,3);
    assert(!nas_pty_write_ready(0));
    uint8_t b=3;
    assert((int64_t)nas_pty_write(0,(uint64_t)(uintptr_t)&b,1)<0);
    for(unsigned i=0;i<64;i++)take(2,77);
    assert(nas_pty_write_ready(0));
    empty();

    /* Remove controlling session, then an ISIG byte must not target an
     * unrelated process group or poison the next PTY. */
    assert(nas_pty_ioctl(1,0x5422,0)==0); /* TIOCNOTTY */
    push(0,3);
    empty();
    (void)other;
    return 0;
}
