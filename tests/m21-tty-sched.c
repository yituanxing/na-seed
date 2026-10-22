#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
/* Same native Linux C PID1 for all three genuine QEMU kernels. */
static void ck(int ok,const char *msg) {
    if(ok)return;
    fprintf(stderr,"[M21] failed: %s errno=%d\n",msg,errno);
    _exit(96);
}
int main(int argc,char **argv) {
    if(argc==2 && strcmp(argv[1],"--linux-pty-oracle")==0) {
        struct termios t;
        ck(isatty(0)&&tcgetattr(0,&t)==0,"host PTY");
        puts("m21-linux-pty-ok");return 0;
    }
    ck(argc==1,"one PID1 ELF");
    ck(ioctl(0,TIOCSCTTY,0)==0,"attach controlling serial");
    int ttyfd=open("/dev/tty",O_RDWR);
    ck(ttyfd>=0,"open genuine /dev/tty");
    struct termios t;
    ck(tcgetattr(ttyfd,&t)==0,"TCGETS");
    t.c_lflag|=ICANON|ECHO;t.c_iflag|=ICRNL;
    ck(tcsetattr(ttyfd,TCSANOW,&t)==0,"TCSETS");
    int startup[2];ck(pipe(startup)==0,"pipe");
    pid_t child=fork();ck(child>=0,"fork");
    if(child==0) {
        close(startup[0]);
        char ready='R';ck(write(startup[1],&ready,1)==1,"ready handshake");
        close(startup[1]);
        char buf[32];
        ssize_t n=read(ttyfd,buf,sizeof buf);
        ck(n==5 && memcmp(buf,"wake\n",5)==0,"read parked UART");
        puts("[M21] child-tty-read-ok");
        fflush(stdout); /* _exit skips stdio flushing on Linux */
        _exit(0);
    }
    close(startup[1]);
    char ready=0;
    ck(read(startup[0],&ready,1)==1 && ready=='R',"pipe wake");
    close(startup[0]);
    puts("[M21] parent-progress");
    fflush(stdout);
    int status=0;
    ck(waitpid(child,&status,0)==child,"waitpid child after tty");
    ck(WIFEXITED(status)&&WEXITSTATUS(status)==0,"child exit");
    puts("[M21] tty-scheduler-ok");
    return 0;
}
