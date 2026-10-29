#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* Unmodified Linux/POSIX PTY interface: compile the SAME C on a native
 * Linux host and as a target-ISA guest executable. Its parent writes to the
 * genuine PTY master; the child owns a new controlling slave/session. */
struct scenario {
    const char *name;
    int canon, isig, custom_intr, map_cr, expected_signal;
    unsigned char input[2];
    unsigned input_bytes, noflsh;
};
static volatile sig_atomic_t observed;
static void caught(int signo) { observed=signo; }
static void require(int ok,const char *why,const char *name) {
    if(ok)return;
    fprintf(stderr,"[M105 PTY ISIG FAIL] case=%s reason=%s errno=%d\n",
            name,why,errno);
    fflush(stderr);
    exit(90);
}
static unsigned char recv_byte(int fd,const char *name) {
    struct pollfd p={.fd=fd,.events=POLLIN};
    require(poll(&p,1,10000)>0 && (p.revents&(POLLIN|POLLHUP)),
            "PTY child handshake timeout",name);
    unsigned char value=0;
    require(read(fd,&value,1)==1,"PTY child handshake read",name);
    return value;
}
static void child_process(const struct scenario *c,int master,const char *slave,
                          int rx,int tx) {
    close(master);close(rx);
    require(setsid()>0,"setsid",c->name);
    int fd=open(slave,O_RDWR|O_NOCTTY);
    require(fd>=0,"open PTY slave",c->name);
    require(ioctl(fd,TIOCSCTTY,0)==0,"TIOCSCTTY",c->name);
    require(tcsetpgrp(fd,getpgrp())==0,"foreground PGID",c->name);
    struct termios t;
    require(tcgetattr(fd,&t)==0,"TCGETS",c->name);
    t.c_lflag &= ~(ECHO|ICANON|ISIG);
    if(c->canon)t.c_lflag|=ICANON;
    if(c->isig)t.c_lflag|=ISIG;
    t.c_iflag &= ~(IGNCR|ICRNL|INLCR);
    if(c->map_cr)t.c_iflag|=ICRNL;
    if(c->noflsh)t.c_lflag|=NOFLSH;
    else t.c_lflag&=~NOFLSH;
    t.c_cc[VINTR]=c->custom_intr?c->custom_intr:3;
    t.c_cc[VQUIT]=28;
    t.c_cc[VSUSP]=26;
    require(tcsetattr(fd,TCSANOW,&t)==0,"TCSETS",c->name);
    struct sigaction act={0};
    act.sa_handler=caught;
    sigemptyset(&act.sa_mask);
    require(sigaction(SIGINT,&act,NULL)==0,"SIGINT handler",c->name);
    require(sigaction(SIGQUIT,&act,NULL)==0,"SIGQUIT handler",c->name);
    require(sigaction(SIGTSTP,&act,NULL)==0,"SIGTSTP handler",c->name);
    unsigned char ready='R';
    require(write(tx,&ready,1)==1,"ready write",c->name);
    unsigned char result=0;
    if(c->expected_signal) {
        sigset_t empty;
        sigemptyset(&empty);
        while(!observed)sigsuspend(&empty);
        result=(unsigned char)observed;
        if(c->noflsh) {
            /* Input entered before VINTR must remain readable, even though
             * the signal handler has already run. Linux is the oracle. */
            struct pollfd p={.fd=fd,.events=POLLIN};
            require(poll(&p,1,10000)>0 && (p.revents&POLLIN),
                    "NOFLSH retained input not readable",c->name);
            unsigned char preserved=0;
            require(read(fd,&preserved,1)==1 && preserved=='A',
                    "NOFLSH must preserve pre-signal input",c->name);
        }
    } else {
        unsigned char bytes[4]={0};
        require(read(fd,bytes,sizeof(bytes))>0,"ISIG-off data read",c->name);
        result=bytes[0];
    }
    require(write(tx,&result,1)==1,"result write",c->name);
    close(fd);close(tx);
    _exit(0);
}
static void run_case(const struct scenario *c) {
    int master=posix_openpt(O_RDWR|O_NOCTTY);
    require(master>=0,"posix_openpt",c->name);
    require(grantpt(master)==0 && unlockpt(master)==0,
            "grantpt/unlockpt",c->name);
    const char *path=ptsname(master);
    require(path && strlen(path)<128,"ptsname",c->name);
    char slave[128];strcpy(slave,path);
    int ready[2];
    require(pipe(ready)==0,"readiness pipe",c->name);
    pid_t child=fork();
    require(child>=0,"fork",c->name);
    if(!child)child_process(c,master,slave,ready[0],ready[1]);
    close(ready[1]);
    require(recv_byte(ready[0],c->name)=='R',"PTY slave ready",c->name);
    require(write(master,c->input,c->input_bytes)==(ssize_t)c->input_bytes,
            "write real PTY master",c->name);
    unsigned char observed_byte=recv_byte(ready[0],c->name);
    unsigned char wanted=(unsigned char)(c->expected_signal?
                     c->expected_signal:
                     c->map_cr && c->input[0]=='\r'?'\n':c->input[0]);
    require(observed_byte==wanted,"ISIG signal/data result",c->name);
    int status=0;
    require(waitpid(child,&status,0)==child && WIFEXITED(status) &&
            WEXITSTATUS(status)==0,"child returned success",c->name);
    close(ready[0]);close(master);
    printf("[M105 PTY ISIG] %s PASS\n",c->name);
}
int main(int argc,char **argv) {
    if(argc>2 || (argc==2 && strcmp(argv[1],"--linux-pty-oracle")))return 2;
    const struct scenario cases[]={
        {"canonical-sigint",1,1,0,0,SIGINT,{3},1,0},
        {"raw-isig-sigint",0,1,0,0,SIGINT,{3},1,0},
        {"raw-isig-sigquit",0,1,0,0,SIGQUIT,{28},1,0},
        {"canonical-sigtstp-handler",1,1,0,0,SIGTSTP,{26},1,0},
        {"raw-no-isig",0,0,0,0,0,{3},1,0},
        {"canonical-no-isig",1,0,0,0,0,{3,'\n'},2,0},
        {"custom-vintr",0,1,'X',0,SIGINT,{'X'},1,0},
        {"icrnl-does-not-remap-vintr",0,1,'\n',1,0,{'\r'},1,0},
        {"icrnl-vintr-before-remap",0,1,'\r',1,SIGINT,{'\r'},1,0},
        {"raw-noflsh-preserves-input",0,1,0,0,SIGINT,{'A',3},2,1},
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)run_case(&cases[i]);
    puts(argc==2?"M105_LINUX_PTY_ISIG_ORACLE_PASS":"M105_NATIVE_PTY_ISIG_PASS");
    return 0;
}
