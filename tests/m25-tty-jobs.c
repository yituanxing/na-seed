#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* One unchanged ordinary Linux program for x86-64, RV64, and AArch64.
 * The PID 1 test runs with an actual physical QEMU serial console.
 * Its children are real job-control process groups in the same session.
 */
static void ck(int success,const char *message) {
    if(success)return;
    fprintf(stderr,"[M25 FAIL] %s errno=%d\n",message,errno);
    fflush(stderr);
    _exit(95);
}
static void say(const char *s) { puts(s);fflush(stdout); }
static void stopped(pid_t child,int signum) {
    int st=-1;
    ck(waitpid(child,&st,WUNTRACED)==child,"waitpid background stop");
    ck(WIFSTOPPED(st) && WSTOPSIG(st)==signum,
       "background job must report exact Linux stop signal");
}
static void exited(pid_t child) {
    int st=-1;
    ck(waitpid(child,&st,0)==child &&
       WIFEXITED(st) && WEXITSTATUS(st)==0,"child exited normally");
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--linux-pty-oracle")==0) {
        struct termios t;
        ck(isatty(0) && tcgetattr(0,&t)==0,"Linux PTY");
        ck(tcgetpgrp(0)>0,"native controlling foreground PGID");
        ck(tcsetattr(0,TCSANOW,&t)==0,"native termios roundtrip");
        say("m25-linux-pty-ok");
        return 0;
    }
    ck(argc==1,"single real Linux C PID1");
    ck(ioctl(0,TIOCSCTTY,0)==0,"attach controlling physical UART");
    int fd=open("/dev/tty",O_RDWR);
    ck(fd>=0 && isatty(fd)==1,"open real /dev/tty");
    pid_t own=getpgrp();
    ck(own>0 && tcgetpgrp(fd)==own,"initial foreground process group");
    struct termios t;
    ck(tcgetattr(fd,&t)==0,"read kernel Linux termios");
    t.c_lflag|=ICANON|ECHO;
    t.c_iflag|=ICRNL;
    ck(tcsetattr(fd,TCSANOW,&t)==0,"activate canonical TTY");

    /* Background read: the child must STOP on SIGTTIN before consuming
     * anything. Parent's waitpid(WUNTRACED) is the only readiness gate. */
    pid_t reader=fork();
    ck(reader>=0,"fork background TTY reader");
    if(reader==0) {
        ck(setpgid(0,0)==0,"reader creates background PGID");
        char bytes[32];
        ssize_t n;
        do { n=read(fd,bytes,sizeof bytes); }
        while(n<0&&errno==EINTR); /* portable syscall restart boundary */
        ck(n==7 && memcmp(bytes,"bgwake\n",7)==0,
           "read actual serial bytes after foreground SIGCONT");
        say("[M25] background-read-ok");
        _exit(0);
    }
    stopped(reader,SIGTTIN);
    say("[M25] sigttin-stop-ok");
    ck(tcsetpgrp(fd,reader)==0,"give physical TTY to reader");
    ck(kill(reader,SIGCONT)==0,"resume stopped foreground reader");
    say("[M25] input-ready"); /* host injects bgwake+CR only now */
    exited(reader);
    ck(signal(SIGTTOU,SIG_IGN)!=SIG_ERR,
       "ignore parent SIGTTOU for foreground restoration");
    ck(tcsetpgrp(fd,own)==0,"restore foreground after reader");
    ck(signal(SIGTTOU,SIG_DFL)!=SIG_ERR,"restore SIGTTOU default");

    /* TOSTOP forces a background writer into SIGTTOU without requiring
     * a serial input byte. Default TOSTOP-off diagnostic UART is unchanged. */
    ck(tcgetattr(fd,&t)==0,"read output job-control flags");
    t.c_lflag|=TOSTOP;
    ck(tcsetattr(fd,TCSANOW,&t)==0,"enable real TOSTOP");
    pid_t writer=fork();ck(writer>=0,"fork background writer");
    if(writer==0) {
        ck(setpgid(0,0)==0,"writer creates background PGID");
        char ch='W';ssize_t n;
        do { n=write(fd,&ch,1); } while(n<0&&errno==EINTR);
        ck(n==1,"foreground writer after SIGCONT");
        say("[M25] background-write-ok");
        _exit(0);
    }
    stopped(writer,SIGTTOU);
    say("[M25] sigttou-stop-ok");
    ck(tcsetpgrp(fd,writer)==0,"give foreground to stopped writer");
    ck(kill(writer,SIGCONT)==0,"resume SIGTTOU job");
    exited(writer);
    ck(signal(SIGTTOU,SIG_IGN)!=SIG_ERR,"ignore when reclaiming TTY");
    ck(tcsetpgrp(fd,own)==0,"restore foreground from writer");
    t.c_lflag&=~TOSTOP;
    ck(tcsetattr(fd,TCSANOW,&t)==0,"reset TOSTOP");

    /* Linux job control exception: ignored SIGTTIN means EIO, not STOP,
     * and must never return fake bytes from the terminal. */
    pid_t ignored=fork();ck(ignored>=0,"fork ignored background reader");
    if(!ignored) {
        ck(setpgid(0,0)==0,"ignored child PGID");
        ck(signal(SIGTTIN,SIG_IGN)!=SIG_ERR,"install SIGTTIN ignore");
        char ch;
        errno=0;
        ck(read(fd,&ch,1)==-1 && errno==EIO,
           "ignored background read must fail Linux EIO");
        say("[M25] ignored-read-eio-ok");
        _exit(0);
    }
    exited(ignored);
    close(fd);
    say("[M25] tty-background-jobs-ok");
    return 0;
}
