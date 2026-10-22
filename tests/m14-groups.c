#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
/* Portable Linux process-group/session test: no NaSeed-specific syscalls.
 * A readiness pipe removes Linux host scheduler vs NaSeed child-first races.
 */
static int pipes[3][2],ready[2];
static void die(const char *where) {
    fprintf(stderr,"m14 groups error: %s errno=%d\n",where,errno);
    _exit(99);
}
static void ck(int ok,const char *where) { if (!ok) die(where); }
static pid_t spawn(int kind,pid_t sid,pid_t inherited_group) {
    pid_t child=fork(); ck(child>=0,"fork");
    if (!child) {
        if (kind==0) {
            ck(setpgid(0,0)==0,"create group");
            ck(getpgrp()==getpid(),"own group");
            ck(getsid(0)==sid,"same session");
            errno=0; ck(setsid()==-1 && errno==EPERM,"leader setsid denied");
        } else if (kind==1) {
            ck(getpgrp()==inherited_group,"inherit group");
            ck(getsid(0)==sid,"inherit session");
        } else {
            ck(setsid()==getpid(),"new session");
            ck(getsid(0)==getpid() && getpgrp()==getpid(),"new session identity");
            errno=0; ck(setpgid(0,0)==-1 && errno==EPERM,"session leader setpgid denied");
        }
        char token=(char)('A'+kind);
        ck(write(ready[1],&token,1)==1,"ready handshake");
        close(ready[1]);close(ready[0]);
        for (int i=0;i<3;++i) close(pipes[i][1]);
        char go=0;
        ck(read(pipes[kind][0],&go,1)==1 && go=='K',"read wake token");
        for (int i=0;i<3;++i) close(pipes[i][0]);
        _exit(0);
    }
    return child;
}
static void await(pid_t selector,pid_t expect) {
    int status=-1;
    pid_t reaped=waitpid(selector,&status,0);
    ck(reaped==expect && WIFEXITED(status) && WEXITSTATUS(status)==0,
       "group-specific wait/reap");
}
int main(void) {
    ck(getpid()>1,"expected user-mode child of init");
    for (int i=0;i<3;++i) ck(pipe(pipes[i])==0,"pipe");
    ck(pipe(ready)==0,"ready pipe");
    pid_t sid=getsid(0),group=getpgrp();
    ck(sid>0 && group>0 && getpgid(0)==group,"self group/session");
    ck(getsid(getpid())==sid,"getsid positive pid");
    errno=0; ck(getpgid(999999)==-1 && errno==ESRCH,"getpgid nonexistent");
    errno=0; ck(setpgid(0,-1)==-1 && errno==EINVAL,"setpgid invalid");
    pid_t first=spawn(0,sid,group);
    pid_t second=spawn(1,sid,group);
    pid_t third=spawn(2,sid,group);
    close(ready[1]);
    for (int i=0;i<3;++i) {
        char signal=0;
        ck(read(ready[0],&signal,1)==1 && signal>='A' && signal<='C',
           "child group readiness");
    }
    close(ready[0]);
    ck(getpgid(first)==first && getsid(first)==sid,"new process group");
    ck(getpgid(second)==group && getsid(second)==sid,"inherited group");
    ck(getpgid(third)==third && getsid(third)==third,"new session");
    errno=0;
    ck(setpgid(third,third)==-1 && errno==EPERM,"cross session change denied");
    int status=-1;
    ck(waitpid(0,&status,WNOHANG)==0,"WNOHANG same group");
    ck(waitpid(-first,&status,WNOHANG)==0,"WNOHANG other group");
    ck(waitpid(-third,&status,WNOHANG)==0,"WNOHANG separate session");
    errno=0;
    ck(waitpid(-999999,&status,WNOHANG)==-1 && errno==ECHILD,"empty group ECHILD");
    char wake='K';
    ck(write(pipes[0][1],&wake,1)==1,"wake group");
    await(-first,first);
    ck(write(pipes[1][1],&wake,1)==1,"wake inherited group");
    await(0,second);
    ck(write(pipes[2][1],&wake,1)==1,"wake separate session");
    await(-third,third);
    errno=0;
    ck(waitpid(-1,&status,WNOHANG)==-1 && errno==ECHILD,"all children reaped");
    for (int i=0;i<3;++i) {close(pipes[i][0]);close(pipes[i][1]);}
    puts("m14-groups-ok");
    return 0;
}
