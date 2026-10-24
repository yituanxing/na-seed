#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/* One portable libc workload. NaSeed mode additionally checks deliberate
 * 48-task capacity; the Linux oracle checks ordinary POSIX semantics. */
#define WAVE_CHILDREN 46
#define WAVES 2
static void fail(const char *why) {
    fprintf(stderr,"m12 process error: %s errno=%d\n",why,errno);
    exit(1);
}
static void wave(int expect_limit, pid_t *last_pid) {
    int fds[2];
    pid_t children[WAVE_CHILDREN+1];
    if (pipe(fds)!=0) fail("pipe");
    pid_t self=getpid();
    for (unsigned i=0;i<WAVE_CHILDREN;i++) {
        pid_t pid=fork();
        if (pid<0) fail("fork wave");
        if (!pid) {
            close(fds[1]);
            char c=0;
            if (getppid()!=self || read(fds[0],&c,1)!=1 || c!='K')
                _exit(21);
            close(fds[0]);
            _exit(0);
        }
        if (pid<=*last_pid) fail("PID reused");
        *last_pid=pid;
        children[i]=pid;
    }
    unsigned n=WAVE_CHILDREN;
    errno=0;
    pid_t extra=fork();
    if (extra==0) {
        close(fds[1]);
        char c=0;
        if (read(fds[0],&c,1)!=1 || c!='K') _exit(22);
        close(fds[0]);
        _exit(0);
    }
    if (extra<0) {
        if (errno!=EAGAIN) fail("fork expected EAGAIN");
    } else {
        if (expect_limit) fail("expected bounded process table EAGAIN");
        if (extra<=*last_pid) fail("extra PID reused");
        *last_pid=extra;
        children[n++]=extra;
    }
    if (expect_limit && extra!=-1) fail("no capacity limit");
    int status=0;
    if (waitpid(-1,&status,WNOHANG)!=0) fail("WNOHANG on blocked children");
    close(fds[0]);
    char tokens[WAVE_CHILDREN+1];
    memset(tokens,'K',sizeof(tokens));
    if (write(fds[1],tokens,n)!=(ssize_t)n) fail("write wake tokens");
    close(fds[1]);
    for (unsigned i=0;i<n;i++) {
        status=-1;
        if (waitpid(children[i],&status,0)!=children[i] ||
            !WIFEXITED(status) || WEXITSTATUS(status)!=0)
            fail("wait/reap child");
    }
    errno=0;
    if (waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD)
        fail("ECHILD after reaping");
}
static void orphan(void) {
    int wake[2],report[2];
    if (pipe(wake) || pipe(report)) fail("orphan pipe");
    pid_t middle=fork();
    if (middle<0) fail("fork intermediate");
    if (!middle) {
        pid_t grandchild=fork();
        if (grandchild<0) _exit(31);
        if (grandchild==0) {
            close(wake[1]);
            close(report[0]);
            char c=0;
            if (read(wake[0],&c,1)!=1 || c!='R') _exit(32);
            char result=(getppid()==1)?'Y':'N';
            if (write(report[1],&result,1)!=1) _exit(33);
            close(report[1]);
            close(wake[0]);
            _exit(result=='Y'?0:34);
        }
        _exit(0);
    }
    close(wake[0]);
    close(report[1]);
    int status=0;
    if (waitpid(middle,&status,0)!=middle || !WIFEXITED(status) ||
        WEXITSTATUS(status)!=0) fail("intermediate wait");
    char wakeup='R',answer=0;
    if (write(wake[1],&wakeup,1)!=1) fail("wake orphan");
    close(wake[1]);
    if (read(report[0],&answer,1)!=1 || answer!='Y')
        fail("orphan not adopted by init");
    if (read(report[0],&answer,1)!=0) fail("orphan fd cleanup");
    close(report[0]);
    errno=0;
    if (waitpid(-1,&status,WNOHANG)!=-1 || errno!=ECHILD)
        fail("unrelated adopted child incorrectly waitable");
}
int main(int argc,char **argv) {
    int limit=argc>1 && strcmp(argv[1],"--naseed-limit")==0;
    /* BusyBox ash tail-execs a single final command. To test all 48
     * slots (PID1 shell + this task + 46 children), the wrapper shell
     * must remain alive as PID1. Never lower the saturation condition. */
    if (limit && getppid()!=1)
        fail("M12 requires a persistent PID1 shell parent");
    pid_t last_pid=0;
    for (int i=0;i<WAVES;i++) wave(limit,&last_pid);
    orphan();
    puts("m12-process-ok");
    return 0;
}
