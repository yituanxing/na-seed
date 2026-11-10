/* Genuine host Linux PTY oracle for NaSeed M188 termios/readv semantics.
 * No mocked terminal driver. Native guest QEMU is a separate final gate. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct peer_write {
    int master;
    char data[2];
    unsigned n;
    unsigned second_ms;
};
static void pause_ms(unsigned ms) {
    struct timespec delay={.tv_sec=ms/1000U,
                           .tv_nsec=(long)(ms%1000U)*1000000L};
    assert(nanosleep(&delay,NULL)==0);
}
static void *peer_main(void *arg) {
    struct peer_write *peer=(struct peer_write *)arg;
    pause_ms(25);
    assert(write(peer->master,peer->data,1)==1);
    if(peer->n>1) {
        pause_ms(peer->second_ms);
        assert(write(peer->master,peer->data+1,1)==1);
    }
    return NULL;
}
static void mode(int slave,unsigned minimum,unsigned deciseconds) {
    struct termios t;
    assert(tcgetattr(slave,&t)==0);
    cfmakeraw(&t);
    t.c_cc[VMIN]=(cc_t)minimum;
    t.c_cc[VTIME]=(cc_t)deciseconds;
    assert(tcsetattr(slave,TCSANOW,&t)==0);
    assert(tcflush(slave,TCIFLUSH)==0);
}
static unsigned long elapsed_ms(struct timespec start,struct timespec end) {
    return (unsigned long)((end.tv_sec-start.tv_sec)*1000 +
           (end.tv_nsec-start.tv_nsec)/1000000L);
}
int main(void) {
    int master=-1,slave=-1;
    assert(openpty(&master,&slave,NULL,NULL,NULL)==0);
    struct pollfd pollfd={.fd=slave,.events=POLLIN};
    char first=0,second=0;
    mode(slave,0,0);
    assert(poll(&pollfd,1,0)==0); /* immediate zero read != POLLIN */
    assert(read(slave,&first,1)==0);
    mode(slave,0,1);
    struct timespec start,end;
    assert(clock_gettime(CLOCK_MONOTONIC,&start)==0);
    assert(read(slave,&first,1)==0);
    assert(clock_gettime(CLOCK_MONOTONIC,&end)==0);
    unsigned long duration=elapsed_ms(start,end);
    assert(duration>=70 && duration<1000);

    for(unsigned minimum=1;minimum<=2;minimum++)
        for(unsigned ds=0;ds<=1;ds++) {
            mode(slave,minimum,ds);
            struct peer_write peer={.master=master,.data={'A','B'},
                                    .n=minimum==2 && ds==0?2U:1U,
                                    .second_ms=25};
            pthread_t writer;
            assert(pthread_create(&writer,NULL,peer_main,&peer)==0);
            struct iovec iov[2]={{.iov_base=&first,.iov_len=1},
                                 {.iov_base=&second,.iov_len=1}};
            ssize_t got=readv(slave,iov,2);
            assert(pthread_join(writer,NULL)==0);
            assert(got==(minimum==2 && ds==0?2:1));
            assert(first=='A');
            if(got==2)assert(second=='B');
        }

    mode(slave,2,1);
    assert(fcntl(slave,F_SETFL,fcntl(slave,F_GETFL)|O_NONBLOCK)==0);
    errno=0;
    assert(read(slave,&first,1)<0 && errno==EAGAIN);
    assert(write(master,"C",1)==1);
    assert(read(slave,&first,1)==1 && first=='C');
    close(slave);close(master);
    puts("[M188 LINUX PTY ORACLE PASS] actual openpty, raw 0/1/2 x 0/1, [1,1] readv, poll and nonblock");
    return 0;
}
