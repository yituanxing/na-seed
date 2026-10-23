#define _GNU_SOURCE
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
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
    /* M31: same genuine boot-RTC epoch and monotonic clocksource on 3 ISAs.
     * The source is executed unchanged against host Linux, then the guest.
     * Neither a fabricated 1970 date nor a per-syscall RTC reread passes. */
    struct timespec realtime_start,realtime_after;
    struct timeval wall_tv;
    ck(clock_gettime(CLOCK_REALTIME,&realtime_start)==0 &&
       realtime_start.tv_sec >= 946684800,"real UNIX boot epoch");
    ck(gettimeofday(&wall_tv,NULL)==0 &&
       wall_tv.tv_sec >= 946684800,"gettimeofday boot epoch");
    fprintf(stderr,"[M31 DIAG] kernel clock_gettime realtime.sec=%lld tv.sec=%lld\\n",
            (long long)realtime_start.tv_sec,(long long)wall_tv.tv_sec);
    uint64_t wall_ns=ns(&realtime_start);
    uint64_t tv_ns=(uint64_t)wall_tv.tv_sec*1000000000ULL+
                   (uint64_t)wall_tv.tv_usec*1000ULL;
    /* CLOCK_REALTIME is adjustable by NTP. The two independent
     * observations must agree, but Linux promises no ordering between
     * consecutive clock_gettime and gettimeofday calls if wall time steps. */
    uint64_t discrepancy=(tv_ns>=wall_ns)?(tv_ns-wall_ns):(wall_ns-tv_ns);
    if(discrepancy>=2000000000ULL)
        fprintf(stderr,"[M31] realtime mismatch clock=%llu gettimeofday=%llu diff=%llu ns\\n",
                (unsigned long long)wall_ns,
                (unsigned long long)tv_ns,
                (unsigned long long)discrepancy);
    ck(discrepancy<2000000000ULL,
       "gettimeofday agrees with realtime");
    struct timespec realtime_deadline=realtime_start;
    realtime_deadline.tv_nsec+=20000000L;
    if(realtime_deadline.tv_nsec>=1000000000L) {
        realtime_deadline.tv_nsec-=1000000000L;
        realtime_deadline.tv_sec++;
    }
    ck(clock_nanosleep(CLOCK_REALTIME,TIMER_ABSTIME,
                       &realtime_deadline,NULL)==0,
       "absolute CLOCK_REALTIME sleep");
    ck(clock_gettime(CLOCK_REALTIME,&realtime_after)==0 &&
       ns(&realtime_after)>=ns(&realtime_deadline),
       "realtime absolute deadline");
    puts("m29-time-ok");
    return 0;
}
