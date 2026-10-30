/* Linux N_TTY oracle: readv() is one logical read, not one read per iovec.
 * This host PTY is a real Linux kernel implementation, not a NaSeed mock. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int master_fd, slave_fd;
static atomic_bool completed;
static ssize_t read_result;
static char a,b;

static void tty_open(unsigned vmin,unsigned vtime) {
    master_fd=posix_openpt(O_RDWR|O_NOCTTY);
    assert(master_fd>=0);
    assert(grantpt(master_fd)==0 && unlockpt(master_fd)==0);
    char *name=ptsname(master_fd);
    assert(name);
    slave_fd=open(name,O_RDWR|O_NOCTTY);
    assert(slave_fd>=0);
    struct termios t;
    assert(tcgetattr(slave_fd,&t)==0);
    cfmakeraw(&t);
    t.c_cc[VMIN]=(cc_t)vmin;t.c_cc[VTIME]=(cc_t)vtime;
    assert(tcsetattr(slave_fd,TCSANOW,&t)==0);
}
static void tty_close(void) { close(slave_fd);close(master_fd); }
static void *reader(void *arg) {
    (void)arg;
    struct iovec v[]={{&a,1},{&b,1}};
    read_result=readv(slave_fd,v,2);
    atomic_store(&completed,true);
    return NULL;
}
static void pause_80ms(void) {
    const struct timespec t={0,80000000L};
    nanosleep(&t,NULL);
}
static void vmin_blocks_on_total(void) {
    tty_open(2,0);
    assert(write(master_fd,"A",1)==1);
    atomic_store(&completed,false);
    pthread_t thread;
    assert(pthread_create(&thread,NULL,reader,NULL)==0);
    pause_80ms();
    assert(!atomic_load(&completed));
    assert(write(master_fd,"B",1)==1);
    assert(pthread_join(thread,NULL)==0);
    assert(read_result==2 && a=='A' && b=='B');
    tty_close();
    puts("[NAS-A013 LINUX TTY VMIN PASS] split 1+1 waits for total two bytes");
}
static void partial_on_unmapped_later_iov(void) {
    tty_open(2,0);
    long page=sysconf(_SC_PAGESIZE);
    assert(page>0);
    void *bad=mmap(NULL,(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(bad!=MAP_FAILED);
    char first='?';
    struct iovec v[]={{&first,1},{bad,1}};
    assert(write(master_fd,"CD",2)==2);
    errno=0;
    ssize_t got=readv(slave_fd,v,2);
    assert(got==1 && first=='C');
    assert(munmap(bad,(size_t)page)==0);
    tty_close();
    puts("[NAS-A013 LINUX TTY PARTIAL PASS] first destination copied before unmapped later iovec");
}
static void nonblock_ignores_vmin(void) {
    tty_open(2,0);
    int flags=fcntl(slave_fd,F_GETFL);
    assert(flags>=0 && fcntl(slave_fd,F_SETFL,flags|O_NONBLOCK)==0);
    assert(write(master_fd,"E",1)==1);
    char first='?',second='?';
    struct iovec v[]={{&first,1},{&second,1}};
    assert(readv(slave_fd,v,2)==1 && first=='E');
    tty_close();
    puts("[NAS-A013 LINUX TTY NONBLOCK PASS] noncanonical O_NONBLOCK ignores VMIN=2");
}
static void canonical_eof_is_one_token(void) {
    tty_open(1,0);
    struct termios t;
    assert(tcgetattr(slave_fd,&t)==0);
    t.c_lflag|=ICANON;
    t.c_cc[VEOF]=4;
    assert(tcsetattr(slave_fd,TCSANOW,&t)==0);
    assert(write(master_fd,"\004",1)==1);
    char first='?';
    struct iovec v[]={{&first,1}};
    assert(readv(slave_fd,v,1)==0);
    int flags=fcntl(slave_fd,F_GETFL);
    assert(flags>=0 && fcntl(slave_fd,F_SETFL,flags|O_NONBLOCK)==0);
    errno=0;
    assert(readv(slave_fd,v,1)==-1 && errno==EAGAIN);
    tty_close();
    puts("[NAS-A013 LINUX TTY EOF PASS] canonical VEOF is retired once");
}
int main(void) {
    vmin_blocks_on_total();
    partial_on_unmapped_later_iov();
    nonblock_ignores_vmin();
    canonical_eof_is_one_token();
    return 0;
}
