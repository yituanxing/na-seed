#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
/* Unmodified Linux C program under native Linux, QEMU-user and the three
 * real shared NaSeed kernels. Readiness pipes remove scheduler races. */
static void ck(int ok,const char *what) {
    if (ok) return;
    fprintf(stderr,"m15: %s errno=%d\n",what,errno);
    _exit(99);
}
static void killed(pid_t target) {
    int status=-1;
    ck(waitpid(target,&status,0)==target,"waitpid");
    ck(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,"WTERMSIG(SIGKILL)");
}
static void ack(int fd,char expected) {
    char byte=0;
    ck(read(fd,&byte,1)==1 && byte==expected,"read handshake");
}
int main(void) {
    ck(getpid()>1,"need a userspace process");
    ck(kill(getpid(),0)==0,"kill self sig0");
    ck(kill(0,0)==0,"kill current group sig0");
    errno=0;
    ck(kill(999999,0)==-1 && errno==ESRCH,"missing pid SIG0");
    errno=0;
    ck(kill(999999,SIGKILL)==-1 && errno==ESRCH,"missing pid SIGKILL");
    errno=0;
    ck(kill(getpid(),65)==-1 && errno==EINVAL,"invalid signal");
    int ready[2],block[2];
    ck(pipe(ready)==0 && pipe(block)==0,"pipes");
    pid_t first=fork();
    ck(first>=0,"single fork");
    if (!first) {
        char yes='A',value;
        ck(write(ready[1],&yes,1)==1,"single ready");
        ck(read(block[0],&value,1)==1,"single blocked");
        _exit(17);
    }
    ack(ready[0],'A');
    ck(kill(first,0)==0,"single exists");
    ck(kill(first,SIGKILL)==0,"kill parked child");
    killed(first);
    errno=0;
    ck(kill(first,0)==-1 && errno==ESRCH,"reaped pid missing");

    pid_t leader=fork();
    ck(leader>=0,"group leader fork");
    if (!leader) {
        ck(setpgid(0,0)==0,"leader group");
        char yes='L',value;
        ck(write(ready[1],&yes,1)==1,"leader ready");
        ck(read(block[0],&value,1)==1,"leader blocked");
        _exit(18);
    }
    ack(ready[0],'L');
    pid_t member=fork();
    ck(member>=0,"group member fork");
    if (!member) {
        ck(setpgid(0,leader)==0,"join group");
        char yes='M',value;
        ck(write(ready[1],&yes,1)==1,"member ready");
        ck(read(block[0],&value,1)==1,"member blocked");
        _exit(19);
    }
    ack(ready[0],'M');
    ck(getpgid(leader)==leader && getpgid(member)==leader,
       "one process group");
    ck(kill(-leader,0)==0,"negative PGID probe");
    ck(kill(-leader,SIGKILL)==0,"kill group");
    killed(leader);
    killed(member);

    pid_t self=fork();
    ck(self>=0,"self-kill fork");
    if (!self) {
        (void)kill(getpid(),SIGKILL);
        _exit(20);
    }
    killed(self);
    close(block[0]); close(block[1]);
    close(ready[0]); close(ready[1]);
    puts("m15-kill-ok");
    return 0;
}
