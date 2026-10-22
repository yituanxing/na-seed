#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
/* A genuine Linux ELF. poll(2) is really ppoll(73) on RV64/AArch64,
 * unlike x86's native poll(7). Same source, same logical event contract. */
static void ck(int ok,const char *msg) {
    if(ok)return;
    fprintf(stderr,"[M22] failed: %s errno=%d\n",msg,errno);
    _exit(96);
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--linux-pty-oracle")==0) {
        struct termios t;
        ck(isatty(0) && tcgetattr(0,&t)==0,"native Linux PTY");
        puts("m22-linux-pty-ok");return 0;
    }
    ck(argc==1,"single guest PID1");
    ck(ioctl(0,TIOCSCTTY,0)==0,"real controlling terminal");
    int fd=open("/dev/tty",O_RDWR);
    ck(fd>=0,"open controlling /dev/tty");
    struct termios tty;
    ck(tcgetattr(fd,&tty)==0,"Linux TCGETS");
    tty.c_iflag|=ICRNL;
    tty.c_lflag|=ICANON|ECHO;
    ck(tcsetattr(fd,TCSANOW,&tty)==0,"Linux TCSETS");
    struct pollfd invalid={.fd=60000,.events=POLLIN};
    ck(poll(&invalid,1,0)==1 && (invalid.revents&POLLNVAL),"invalid FD POLLNVAL");
    struct pollfd terminal={.fd=fd,.events=POLLIN};
    ck(poll(&terminal,1,0)==0 && !terminal.revents,
       "TTY canonical poll not ready before physical input");

    int handoff[2];ck(pipe(handoff)==0,"handoff pipe");
    pid_t child=fork();ck(child>=0,"fork tty poll child");
    if(child==0) {
        close(handoff[0]);
        ck(write(handoff[1],"R",1)==1,"child readiness");
        close(handoff[1]);
        terminal.revents=0;
        ck(poll(&terminal,1,-1)==1 && (terminal.revents&POLLIN),
           "blocking tty poll after real serial input");
        char line[16]={0};
        ck(read(fd,line,sizeof line)==5 && !memcmp(line,"poll\n",5),
           "TTY read from poll readiness");
        puts("[M22] child-tty-poll-ok");
        fflush(stdout);
        _exit(0);
    }
    close(handoff[1]);
    char ready=0;
    ck(read(handoff[0],&ready,1)==1 && ready=='R',"real pipe handshake");
    close(handoff[0]);
    puts("[M22] parent-progress");
    fflush(stdout);
    int status;
    ck(waitpid(child,&status,0)==child && WIFEXITED(status) &&
       WEXITSTATUS(status)==0,"reap awakened tty child");

    int pipefd[2];ck(pipe(pipefd)==0,"poll pipe setup");
    pid_t pipe_child=fork();ck(pipe_child>=0,"fork pipe poll child");
    if(pipe_child==0) {
        close(pipefd[1]);
        struct pollfd p={.fd=pipefd[0],.events=POLLIN};
        ck(poll(&p,1,0)==0,"empty pipe poll=0");
        ck(poll(&p,1,-1)==1 && (p.revents&POLLIN),"pipe blocking poll wake");
        char b=0;
        ck(read(pipefd[0],&b,1)==1 && b=='X',"actual pipe data after poll");
        puts("[M22] pipe-poll-ok");
        fflush(stdout);
        _exit(0);
    }
    close(pipefd[0]);
    ck(write(pipefd[1],"X",1)==1,"parent pipe write");
    close(pipefd[1]);
    ck(waitpid(pipe_child,&status,0)==pipe_child && WIFEXITED(status) &&
       WEXITSTATUS(status)==0,"reap pipe poll child");
    close(fd);
    puts("[M22] poll-events-ok");
    return 0;
}
