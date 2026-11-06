/* M205 native PTY test fixture, NOT an original Alpine acceptance ELF.
 *
 * Wait for real application prompts on an allocated controlling terminal,
 * send replies only afterward, then require the actual child exit status.
 * This exercises NaSeed's real ptmx/pts/ioctl/read/write/poll/fork/exec path.
 * It never produces a canned success token or depends on named FIFO support.
 *
 * Usage: m205-pty-exchange TRANSCRIPT PROMPT1 REPLY1 PROMPT2 REPLY2
 *                         COMMAND [ARGS...]
 * For one prompt use "-" as both PROMPT2 and REPLY2.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef TIOCGPTN
#define TIOCGPTN 0x80045430U
#endif
#ifndef TIOCSPTLCK
#define TIOCSPTLCK 0x40045431U
#endif

#define M205_PTY_HISTORY 8192U
#define M205_PTY_TIMEOUT_NS (30ULL * 1000000000ULL)

static void error(const char *detail) {
    fprintf(stderr,"M205 PTY fixture: %s (errno=%d)\n",detail,errno);
}

static uint64_t monotonic_ns(void) {
    struct timespec clock_value;
    if(clock_gettime(CLOCK_MONOTONIC,&clock_value)<0)return 0;
    return (uint64_t)clock_value.tv_sec*1000000000ULL+
           (uint64_t)clock_value.tv_nsec;
}

static bool write_all(int fd,const char *data,size_t len) {
    while(len) {
        ssize_t wrote=write(fd,data,len);
        if(wrote<0 && errno==EINTR)continue;
        if(wrote<=0)return false;
        data+=(size_t)wrote;
        len-=(size_t)wrote;
    }
    return true;
}

static bool send_reply(int master,const char *reply) {
    return write_all(master,reply,strlen(reply)) &&
           write_all(master,"\n",1);
}

static void remember(char history[M205_PTY_HISTORY+1],size_t *length,
                     const char *data,size_t count) {
    if(count>M205_PTY_HISTORY) {
        data+=count-M205_PTY_HISTORY;
        count=M205_PTY_HISTORY;
    }
    if(*length+count>M205_PTY_HISTORY) {
        size_t remove=*length+count-M205_PTY_HISTORY;
        memmove(history,history+remove,*length-remove);
        *length-=remove;
    }
    memcpy(history+*length,data,count);
    *length+=count;
    history[*length]=0;
}

/* Child uses its own session so the slave really becomes the controlling TTY.
 * The master is never a substitute for user input: the caller observes the
 * original command output before transmitting any password/line. */
static void child_exec(unsigned number,int master,char *const argv[]) {
    if(setsid()<0)_exit(125);
    char path[32];
    if(snprintf(path,sizeof(path),"/dev/pts/%u",number)>= (int)sizeof(path))
        _exit(125);
    int slave=open(path,O_RDWR|O_NOCTTY);
    if(slave<0)_exit(125);
    if(ioctl(slave,TIOCSCTTY,0)<0)_exit(125);
    if(dup2(slave,STDIN_FILENO)<0 || dup2(slave,STDOUT_FILENO)<0 ||
       dup2(slave,STDERR_FILENO)<0)_exit(125);
    if(slave>STDERR_FILENO)close(slave);
    close(master);
    execvp(argv[0],argv);
    _exit(127);
}

static int exchange(int master,int transcript,pid_t child,
                    const char *prompt1,const char *reply1,
                    const char *prompt2,const char *reply2) {
    char history[M205_PTY_HISTORY+1]={0};
    size_t history_len=0;
    int next=0;
    const int required=strcmp(prompt2,"-")==0?1:2;
    int status=0;
    bool reaped=false,eof=false;
    uint64_t started=monotonic_ns();
    if(!started) { error("no monotonic time source");return 1; }
    while(!eof || !reaped) {
        if(monotonic_ns()-started>M205_PTY_TIMEOUT_NS) {
            error("real PTY prompt/child timed out");
            return 1;
        }
        struct pollfd input={.fd=master,.events=POLLIN|POLLHUP};
        int polled=poll(&input,1,250);
        if(polled<0 && errno!=EINTR) {
            error("poll master PTY");return 1;
        }
        if(polled>0 && (input.revents&(POLLIN|POLLHUP|POLLERR))) {
            char data[1024];
            ssize_t got=read(master,data,sizeof(data));
            if(got>0) {
                if(!write_all(transcript,data,(size_t)got)) {
                    error("write real PTY transcript");return 1;
                }
                remember(history,&history_len,data,(size_t)got);
                if(next==0 && strstr(history,prompt1)) {
                    if(!send_reply(master,reply1)) {
                        error("send first reply after real prompt");return 1;
                    }
                    next=1;
                }
                if(next==1 && required==2 && strstr(history,prompt2)) {
                    if(!send_reply(master,reply2)) {
                        error("send second reply after real prompt");return 1;
                    }
                    next=2;
                }
            } else if(got==0 || (got<0 && errno==EIO)) {
                eof=true;
            } else if(got<0 && errno!=EINTR && errno!=EAGAIN) {
                error("read master PTY");return 1;
            }
        }
        if(!reaped) {
            pid_t waited=waitpid(child,&status,WNOHANG);
            if(waited==child)reaped=true;
            else if(waited<0 && errno!=EINTR) {
                error("waitpid real application");return 1;
            }
        }
    }
    if(next!=required) {
        fprintf(stderr,"M205 PTY fixture: observed only %d/%d real prompts\n",
                next,required);
        return 1;
    }
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
        fprintf(stderr,"M205 PTY fixture: genuine application status=%d\n",
                status);
        return 1;
    }
    return 0;
}

int main(int argc,char **argv) {
    if(argc<7 || !*argv[2] || !*argv[3] ||
       (strcmp(argv[4],"-")==0)!=(strcmp(argv[5],"-")==0)) {
        fputs("M205 PTY fixture: invalid command/prompt arguments\n",stderr);
        return 2;
    }
    int transcript=open(argv[1],O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(transcript<0) { error("open transcript");return 1; }
    int master=open("/dev/ptmx",O_RDWR|O_NOCTTY);
    if(master<0) { error("open /dev/ptmx");close(transcript);return 1; }
    unsigned number=0;
    int unlock=0;
    if(ioctl(master,TIOCGPTN,&number)<0 ||
       ioctl(master,TIOCSPTLCK,&unlock)<0) {
        error("configure actual ptmx/pts");
        close(master);close(transcript);return 1;
    }
    pid_t child=fork();
    if(child<0) { error("fork real application");close(master);close(transcript);return 1; }
    if(child==0)child_exec(number,master,&argv[6]);
    int result=exchange(master,transcript,child,argv[2],argv[3],argv[4],argv[5]);
    if(result) {
        /* Never leave an Alpine process blocked on a terminal when an oracle
         * failed. Do not signal a PID already reaped by exchange(): it may
         * have been recycled for an unrelated guest process. */
        int status=0;
        if(waitpid(child,&status,WNOHANG)==0) {
            kill(child,SIGTERM);
            kill(child,SIGKILL);
            (void)waitpid(child,0,0);
        }
    }
    close(master);close(transcript);
    return result;
}
