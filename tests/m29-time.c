#define _GNU_SOURCE
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

/* One native Linux ELF on x86-64, RV64, and AArch64: real seconds must
 * advance, relative sleeps must not return early, a sleeping sole PID1
 * must resume without another runnable task, and empty poll/ppoll must
 * timeout rather than receive a fabricated immediate success. */
static void ck(int ok,const char *s) {
    if(ok)return;
    fprintf(stderr,"[M29] failed: %s errno=%d\n",s,errno);
    _exit(99);
}
static uint64_t ns(const struct timespec *t) {
    return (uint64_t)t->tv_sec*1000000000ULL+(uint64_t)t->tv_nsec;
}
int main(void) {
    struct timespec start,after,req={0,40000000L},res;
    ck(clock_gettime(CLOCK_MONOTONIC,&start)==0,"monotonic available");
    ck(clock_getres(CLOCK_MONOTONIC,&res)==0 &&
       res.tv_sec==0 && res.tv_nsec>0 &&
       res.tv_nsec<=10000000L,"real monotonic resolution");
    ck(nanosleep(&req,0)==0,"relative nanosleep");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)-ns(&start)>=40000000ULL,"nanosleep did not elapse");
    start=after;
    ck(clock_nanosleep(CLOCK_MONOTONIC,0,&req,0)==0,
       "relative clock_nanosleep");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)-ns(&start)>=40000000ULL,"relative clock_nanosleep time");
    struct timespec absolute=after;
    absolute.tv_nsec+=40000000L;
    if(absolute.tv_nsec>=1000000000L) {
        absolute.tv_nsec-=1000000000L;
        absolute.tv_sec++;
    }
    ck(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&absolute,0)==0,
       "absolute monotonic sleep");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)>=ns(&absolute),"absolute sleep deadline");
    start=after;
    ck(poll(NULL,0,40)==0,"poll empty-fd finite timeout");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)-ns(&start)>=40000000ULL,"poll timeout elapsed");
    start=after;
    ck(ppoll(NULL,0,&req,NULL)==0,"ppoll empty-fd finite timeout");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)-ns(&start)>=40000000ULL,"ppoll timeout elapsed");
    int pair[2];
    ck(pipe(pair)==0,"pipe creation");
    struct pollfd f={.fd=pair[0],.events=POLLIN};
    ck(poll(&f,1,0)==0,"empty pipe immediate poll");
    start=after;
    ck(poll(&f,1,40)==0,"empty pipe finite timeout");
    ck(clock_gettime(CLOCK_MONOTONIC,&after)==0 &&
       ns(&after)-ns(&start)>=40000000ULL,"pipe timeout elapsed");
    ck(write(pair[1],"X",1)==1,"pipe ready data");
    ck(poll(&f,1,40)==1 && (f.revents&POLLIN),"ready pipe beats deadline");
    close(pair[0]);close(pair[1]);
    puts("m29-time-ok");
    return 0;
}
