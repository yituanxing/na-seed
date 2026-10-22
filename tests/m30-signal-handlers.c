#define _GNU_SOURCE
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <poll.h>
#include <ucontext.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Identical native GNU/Linux C source for three different target ABIs.
 * This checks a REAL kernel-built signal frame and libc restorer path,
 * not direct calls to a handler and not a synthetic kernel exit marker.
 * NaSeed must pass the Linux oracle before adding it to guest matrix. */
static volatile sig_atomic_t observed_usr1;
static volatile sig_atomic_t observed_usr2;
static volatile sig_atomic_t observed_info;
static volatile sig_atomic_t observed_mask;

static void die(const char *what) {
    perror(what);
    _exit(99);
}
static void require(int ok,const char *what) {
    if (!ok) die(what);
}
static void handler_info(int sig,siginfo_t *info,void *opaque) {
    if (sig!=SIGUSR1 || !info || info->si_signo!=SIGUSR1 || !opaque)
        _exit(98);
    const ucontext_t *uc=(const ucontext_t *)opaque;
    if (!uc) _exit(97);
    observed_info=1;
    sigset_t blocked;
    if (sigprocmask(SIG_BLOCK,NULL,&blocked) != 0) _exit(96);
    observed_mask=sigismember(&blocked,SIGUSR1)==1;
    observed_usr1++;
}
static void handler_simple(int sig) {
    if(sig!=SIGUSR2) _exit(95);
    observed_usr2++;
}
static void install(void) {
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction=handler_info;
    sa.sa_flags=SA_SIGINFO;
    require(sigaction(SIGUSR1,&sa,NULL)==0,"install SA_SIGINFO SIGUSR1");
    memset(&sa,0,sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler=handler_simple;
    require(sigaction(SIGUSR2,&sa,NULL)==0,"install SIGUSR2 handler");
}
int main(void) {
    install();
    require(kill(getpid(),SIGUSR1)==0,"self signal");
    require(observed_usr1==1 && observed_info==1 && observed_mask==1,
            "handler invoked with siginfo/context and current mask");
    require(kill(getpid(),SIGUSR2)==0,"ordinary handler");
    require(observed_usr2==1,"ordinary handler actually returned");
    /* Blocked standard signals coalesce. Pending remains masked until
     * SIG_UNBLOCK; delivery must happen before userspace continuation. */
    sigset_t mask,pending;
    sigemptyset(&mask);
    sigaddset(&mask,SIGUSR1);
    require(sigprocmask(SIG_BLOCK,&mask,NULL)==0,"block SIGUSR1");
    require(kill(getpid(),SIGUSR1)==0 && kill(getpid(),SIGUSR1)==0,
            "enqueue two copies of standard signal");
    require(observed_usr1==1,"masked signal not run");
    require(sigpending(&pending)==0 && sigismember(&pending,SIGUSR1)==1,
            "pending signal visible");
    require(sigprocmask(SIG_UNBLOCK,&mask,NULL)==0,"unmask SIGUSR1");
    require(observed_usr1==2,"unmask delivers exactly once");
    /* Deterministic interrupted-wait test: block SIGUSR2 in child, send it
     * after the explicit pipe handshake, then ppoll atomically temporarily
     * unblocks SIGUSR2. The pending signal necessarily interrupts ppoll
     * regardless of whether the parent ran just before or during the wait.
     * No probabilistic sleep-before-kill synchronization. */
    int gate[2];
    require(pipe(gate)==0,"handshake pipe");
    pid_t child=fork();
    require(child>=0,"fork for interrupted ppoll");
    if(child==0) {
        close(gate[0]);
        observed_usr2=0;
        sigset_t blocked,oldmask;
        sigemptyset(&blocked);
        sigaddset(&blocked,SIGUSR2);
        require(sigprocmask(SIG_BLOCK,&blocked,&oldmask)==0,"child block");
        require(write(gate[1],"R",1)==1,"child handshake");
        close(gate[1]);
        struct timespec one={1,0};
        errno=0;
        int rc=ppoll(NULL,0,&one,&oldmask);
        require(rc==-1 && errno==EINTR,"ppoll interrupted by pending signal");
        require(observed_usr2==1,"caught signal ran during ppoll");
        sigset_t after;
        require(sigprocmask(SIG_BLOCK,NULL,&after)==0 &&
                sigismember(&after,SIGUSR2)==1,"ppoll restored saved mask");
        _exit(0);
    }
    close(gate[1]);
    char ready=0;
    require(read(gate[0],&ready,1)==1 && ready=='R',"child ready handshake");
    close(gate[0]);
    require(kill(child,SIGUSR2)==0,"signal blocked ppoll child");
    int status=0;
    require(waitpid(child,&status,0)==child &&
            WIFEXITED(status) && WEXITSTATUS(status)==0,
            "interrupted child resumed and exited");
    puts("m30-signal-ok");
    return 0;
}
