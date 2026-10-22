#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
/* One unchanged native Linux program, run on Linux, qemu-user and three
 * complete NaSeed kernels. No kernel-specific or direct synthetic syscalls.
 * Readiness handshakes eliminate Linux vs child-first kernel scheduler races. */
static void ck(int ok, const char *where) {
    if (ok) return;
    fprintf(stderr,"m16: %s errno=%d\n",where,errno);
    _exit(99);
}
static void ready(int fd,char expected) {
    char got=0;
    ck(read(fd,&got,1)==1 && got==expected,"child ready");
}
static void stopped(pid_t p) {
    int status=-1;
    ck(waitpid(p,&status,WUNTRACED)==p,"wait WUNTRACED");
    ck(WIFSTOPPED(status) && WSTOPSIG(status)==SIGSTOP,"WIFSTOPPED SIGSTOP");
    ck(waitpid(p,&status,WUNTRACED|WNOHANG)==0,"stop event one-shot");
}
static void continued(pid_t p) {
    int status=-1;
    ck(waitpid(p,&status,WCONTINUED)==p,"wait WCONTINUED");
    ck(WIFCONTINUED(status),"WIFCONTINUED state");
    ck(waitpid(p,&status,WCONTINUED|WNOHANG)==0,"continue event one-shot");
}
static void killed(pid_t p) {
    int status=-1;
    ck(waitpid(p,&status,0)==p,"wait signaled exit");
    ck(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL,
       "wait WIFSIGNALED SIGKILL");
}
int main(void) {
    ck(getpid()>1,"need parent of native fork children");
    int sync[2],gate[2];
    ck(pipe(sync)==0 && pipe(gate)==0,"pipes");
    pid_t leader=fork();
    ck(leader>=0,"fork leader");
    if (!leader) {
        ck(setpgid(0,0)==0,"leader pgid");
        char tag='L',go=0;
        ck(write(sync[1],&tag,1)==1,"leader sync");
        ck(read(gate[0],&go,1)==1,"leader gate");
        _exit(21); /* never runs after SIGKILL */
    }
    ready(sync[0],'L');
    pid_t member=fork();
    ck(member>=0,"fork group member");
    if (!member) {
        ck(setpgid(0,leader)==0,"join leader group");
        char tag='M',go=0;
        ck(write(sync[1],&tag,1)==1,"member sync");
        ck(read(gate[0],&go,1)==1,"member gate");
        _exit(22);
    }
    ready(sync[0],'M');
    ck(getpgid(leader)==leader && getpgid(member)==leader,
       "two real children in one group");
    ck(kill(-leader,SIGSTOP)==0,"group SIGSTOP");
    stopped(leader);
    stopped(member);
    ck(kill(leader,0)==0 && kill(-leader,0)==0,"stopped processes exist");
    ck(kill(-leader,SIGCONT)==0,"group SIGCONT");
    continued(leader);
    continued(member);
    ck(kill(-leader,SIGKILL)==0,"group SIGKILL after resume");
    killed(leader);
    killed(member);

    /* Self SIGSTOP must save full user context, switch to parent, and
     * resume the exact post-syscall PC after parent's SIGCONT. */
    pid_t self=fork();
    ck(self>=0,"fork self-stop");
    if (!self) {
        char tag='S',go=0;
        ck(write(sync[1],&tag,1)==1,"self-stop sync");
        ck(kill(getpid(),SIGSTOP)==0,"self-stop resumed");
        ck(read(gate[0],&go,1)==1 && go=='R',"resumed syscall PC");
        _exit(0);
    }
    ready(sync[0],'S');
    stopped(self);
    ck(kill(self,SIGCONT)==0,"resume self-stop child");
    continued(self);
    char go='R';
    ck(write(gate[1],&go,1)==1,"release resumed task");
    int st=-1;
    ck(waitpid(self,&st,0)==self && WIFEXITED(st) && WEXITSTATUS(st)==0,
       "resumed child exits normally");
    close(gate[0]);close(gate[1]);close(sync[0]);close(sync[1]);
    puts("m16-stop-ok");
    return 0;
}
