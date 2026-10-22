#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
/* One unmodified POSIX/Linux C source on host Linux and NaSeed
 * x86-64/RV64/ARM64 full kernels. No raw NaSeed or fake signal syscalls. */
static void ck(int valid, const char *message) {
    if (valid) return;
    fprintf(stderr,"[M24 FAIL] %s errno=%d\n",message,errno);
    fflush(stderr);
    _exit(97);
}
static void got(int fd,char expected) {
    char b=0;
    ck(read(fd,&b,1)==1 && b==expected,"ready handshake");
}
static void send_byte(int fd,char value) {
    ck(write(fd,&value,1)==1,"release pipe write");
}
static void signal_exit(pid_t pid,int number) {
    int status=-1;
    ck(waitpid(pid,&status,0)==pid,"waitpid signaled");
    ck(WIFSIGNALED(status)&&WTERMSIG(status)==number,
       "signal exit status preserved");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--inherited-ignored")) {
        struct sigaction act;
        ck(sigaction(SIGTERM,NULL,&act)==0 && act.sa_handler==SIG_IGN,
           "SIG_IGN persists across execve");
        ck(kill(getpid(),SIGTERM)==0,"ignored self SIGTERM");
        puts("[M24] exec-ignored-ok");fflush(stdout);
        return 0;
    }
    ck(argc==1 || (argc==2 && !strcmp(argv[1],"--linux-oracle")),
       "known native test mode");
    int ready[2],release[2];
    ck(pipe(ready)==0 && pipe(release)==0,"create two real pipes");
    pid_t blocked=fork();
    ck(blocked>=0,"fork blocked child");
    if(!blocked) {
        sigset_t mask;
        sigemptyset(&mask);sigaddset(&mask,SIGTERM);
        ck(sigprocmask(SIG_BLOCK,&mask,NULL)==0,"block SIGTERM");
        send_byte(ready[1],'B');
        got(release[0],'R'); /* must not die while signal blocked */
        sigset_t pending;
        ck(sigpending(&pending)==0 && sigismember(&pending,SIGTERM)==1,
           "genuine rt_sigpending reports blocked signal");
        puts("[M24] pending-mask-ok");fflush(stdout);
        ck(sigprocmask(SIG_UNBLOCK,&mask,NULL)==0,
           "pending SIGTERM should terminate BEFORE return");
        ck(0,"unblocking pending default SIGTERM returned unexpectedly");
    }
    got(ready[0],'B');
    ck(kill(blocked,SIGTERM)==0,"send masked SIGTERM");
    send_byte(release[1],'R');
    signal_exit(blocked,SIGTERM);

    pid_t ignored=fork();
    ck(ignored>=0,"fork ignored child");
    if(!ignored) {
        struct sigaction act;
        memset(&act,0,sizeof act);
        act.sa_handler=SIG_IGN;
        ck(sigaction(SIGTERM,&act,NULL)==0,"install SIG_IGN");
        send_byte(ready[1],'I');
        got(release[0],'G');
        sigset_t pending;
        ck(sigpending(&pending)==0 && sigismember(&pending,SIGTERM)==0,
           "ignored SIGTERM not queued");
        puts("[M24] ignored-signal-ok");fflush(stdout);
        _exit(24);
    }
    got(ready[0],'I');
    ck(kill(ignored,SIGTERM)==0,"send ignored SIGTERM");
    send_byte(release[1],'G');
    int status=-1;
    ck(waitpid(ignored,&status,0)==ignored && WIFEXITED(status) &&
       WEXITSTATUS(status)==24,"ignored child survives signal");

    pid_t self=fork();
    ck(self>=0,"fork self-kill child");
    if(!self) {
        ck(kill(getpid(),SIGTERM)==0,"self default SIGTERM must terminate");
        ck(0,"self-kill returned to userspace");
    }
    signal_exit(self,SIGTERM);
    puts("[M24] self-signal-ok");fflush(stdout);

    if(argc==1) {
        pid_t ex=fork();
        ck(ex>=0,"fork ignored-across-exec");
        if(!ex) {
            struct sigaction act;
            memset(&act,0,sizeof act);act.sa_handler=SIG_IGN;
            ck(sigaction(SIGTERM,&act,NULL)==0,"set ignore before exec");
            char *const args[]={"/bin/m24-signals","--inherited-ignored",NULL};
            execv(args[0],args);
            ck(0,"execv native image");
        }
        ck(waitpid(ex,&status,0)==ex && WIFEXITED(status) &&
           WEXITSTATUS(status)==0,"SIG_IGN inherited through execve");
    }
    close(ready[0]);close(ready[1]);close(release[0]);close(release[1]);
    puts("m24-signals-ok");fflush(stdout);
    return 0;
}
