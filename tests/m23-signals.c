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
/* Real standard Linux/POSIX C: one ELF source for each QEMU-booted ISA. */
static void ck(int yes,const char *why) {
    if(yes)return;
    fprintf(stderr,"[M23 FAIL] %s errno=%d\n",why,errno);
    fflush(stderr);
    _exit(95);
}
static void take(int pipefd,char want) {
    char c=0;
    ck(read(pipefd,&c,1)==1 && c==want,"real child readiness pipe");
}
static void send(char c,int fd) {
    ck(write(fd,&c,1)==1,"pipe readiness handshake");
}
static void block_tty(int ttyfd) {
    char buf[32];
    ssize_t got=read(ttyfd,buf,sizeof buf);
    /* Default SIGINT and SIGTSTP must prevent this read completing. */
    fprintf(stderr,"[M23 FAIL] blocked reader unexpectedly returned %zd\n",got);
    fflush(stderr);
    _exit(94);
}
static void reaped(int pid,int signum) {
    int status=0;
    ck(waitpid(pid,&status,0)==pid,"waitpid child death");
    ck(WIFSIGNALED(status) && WTERMSIG(status)==signum,"WIFSIGNALED exact signum");
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--linux-pty-oracle")) {
        ck(isatty(0),"real Linux PTY");
        struct termios x;ck(tcgetattr(0,&x)==0,"native TCGETS");
        puts("m23-linux-pty-ok");return 0;
    }
    ck(argc==1,"single native PID1");
    ck(ioctl(0,TIOCSCTTY,0)==0,"attach real physical controlling tty");
    int ttyfd=open("/dev/tty",O_RDWR);
    ck(ttyfd>=0,"open controlling /dev/tty");
    struct termios term;
    ck(tcgetattr(ttyfd,&term)==0,"Linux termios read");
    term.c_iflag|=ICRNL;
    term.c_lflag|=ISIG|ICANON|ECHO;
    ck(tcsetattr(ttyfd,TCSANOW,&term)==0,"enable ISIG canonical");
    int ready[2];
    ck(pipe(ready)==0,"create pipe");
    pid_t leader=fork();ck(leader>=0,"fork foreground leader");
    if(!leader) {
        ck(setpgid(0,0)==0,"leader setpgid");
        send('L',ready[1]);
        block_tty(ttyfd);
    }
    take(ready[0],'L');
    pid_t member=fork();ck(member>=0,"fork foreground member");
    if(!member) {
        ck(setpgid(0,leader)==0,"member joins leader PGID");
        send('M',ready[1]);
        block_tty(ttyfd);
    }
    take(ready[0],'M');
    ck(getpgid(leader)==leader && getpgid(member)==leader,
       "two actual children in foreground group");
    ck(tcsetpgrp(ttyfd,leader)==0,"foreground group for SIGINT");
    puts("[M23] int-parent-ready");fflush(stdout);
    reaped(leader,SIGINT);
    reaped(member,SIGINT);
    puts("[M23] sigint-group-ok");fflush(stdout);
    ck(tcsetpgrp(ttyfd,getpgrp())==0,"restore owning shell foreground");

    pid_t stopped=fork();ck(stopped>=0,"fork suspend child");
    if(!stopped) {
        ck(setpgid(0,0)==0,"suspend child PGID");
        send('S',ready[1]);
        block_tty(ttyfd);
    }
    take(ready[0],'S');
    ck(tcsetpgrp(ttyfd,stopped)==0,"foreground group for SIGTSTP");
    puts("[M23] stop-parent-ready");fflush(stdout);
    int status=0;
    ck(waitpid(stopped,&status,WUNTRACED)==stopped,"wait for terminal stop");
    ck(WIFSTOPPED(status)&&WSTOPSIG(status)==SIGTSTP,
       "WIFSTOPPED must report actual SIGTSTP, not SIGSTOP");
    puts("[M23] sigtstp-stop-ok");fflush(stdout);
    ck(kill(stopped,SIGCONT)==0,"resume actual stopped child");
    ck(waitpid(stopped,&status,WCONTINUED)==stopped && WIFCONTINUED(status),
       "WIFCONTINUED after Ctrl+Z");
    ck(kill(stopped,SIGKILL)==0,"kill resumed child");
    reaped(stopped,SIGKILL);
    ck(tcsetpgrp(ttyfd,getpgrp())==0,"final parent foreground");
    close(ready[0]);close(ready[1]);close(ttyfd);
    puts("[M23] tty-signals-ok");
    return 0;
}
