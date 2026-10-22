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
static volatile sig_atomic_t observed_simple_mask;
static volatile sig_atomic_t observed_sigpipe;
static volatile sig_atomic_t observed_sigint;

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
    sigset_t blocked;
    if(sigprocmask(SIG_BLOCK,NULL,&blocked)!=0)_exit(94);
    observed_simple_mask=sigismember(&blocked,SIGUSR2);
    observed_usr2++;
}
static void handler_pipe(int sig) {
    if(sig!=SIGPIPE)_exit(93);
    observed_sigpipe++;
}
static void handler_int(int sig) {
    if(sig!=SIGINT)_exit(92);
    observed_sigint++;
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
int main(int argc,char **argv) {
    install();
    require(kill(getpid(),SIGUSR1)==0,"self signal");
    require(observed_usr1==1 && observed_info==1 && observed_mask==1,
            "handler invoked with siginfo/context and current mask");
    require(kill(getpid(),SIGUSR2)==0,"ordinary handler");
    require(observed_usr2==1 && observed_simple_mask==1,
            "ordinary handler runs with automatic signal block");
    /* Linux flag values are intentionally tested separately: SA_NODEFER
     * (0x40000000) changes the mask but does NOT reset the disposition,
     * while SA_RESETHAND (0x80000000) resets it to SIG_DFL. */
    struct sigaction attr,previous;
    memset(&attr,0,sizeof(attr));
    sigemptyset(&attr.sa_mask);
    attr.sa_handler=handler_simple;
    attr.sa_flags=SA_NODEFER | SA_RESETHAND;
    require(sigaction(SIGUSR2,&attr,NULL)==0,"SA_NODEFER/RESETHAND setup");
    require(kill(getpid(),SIGUSR2)==0,"one-shot unblocked signal");
    require(observed_usr2==2 && observed_simple_mask==0,
            "SA_NODEFER leaves caught signal unblocked");
    require(sigaction(SIGUSR2,NULL,&previous)==0 &&
            previous.sa_handler==SIG_DFL,
            "SA_RESETHAND reset disposition, not just mask");
    /* Reinstall ordinary handler for interrupted-ppoll child below. */
    install();
    memset(&attr,0,sizeof(attr));
    sigemptyset(&attr.sa_mask);
    attr.sa_handler=handler_pipe;
    require(sigaction(SIGPIPE,&attr,NULL)==0,"SIGPIPE handler install");
    int pipefd[2];
    require(pipe(pipefd)==0,"broken pipe creation");
    require(close(pipefd[0])==0,"close broken pipe reader");
    errno=0;
    require(write(pipefd[1],"X",1)==-1 && errno==EPIPE &&
            observed_sigpipe==1,"SIGPIPE handler with real EPIPE");
    require(close(pipefd[1])==0,"close broken pipe writer");
    memset(&attr,0,sizeof(attr));
    sigemptyset(&attr.sa_mask);
    attr.sa_handler=handler_int;
    require(sigaction(SIGINT,&attr,NULL)==0,"SIGINT handler install");
    require(kill(getpid(),SIGINT)==0 && observed_sigint==1,
            "caught SIGINT does not kill the process");
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
    if(argc==2 && strcmp(argv[1],"--self-only")==0) {
        puts("m30-basic-ok");
        return 0;
    }
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
    /* Interruption of a relative sleep must return EINTR plus a bounded
     * remaining timespec. This exercises M29 deadline ownership through
     * M30's signal-driven blocked Task replay, not merely an I/O wait. */
    int sleep_gate[2];
    require(pipe(sleep_gate)==0,"sleep handshake pipe");
    child=fork();
    require(child>=0,"fork interrupted nanosleep");
    if(child==0) {
        close(sleep_gate[0]);
        observed_usr2=0;
        require(write(sleep_gate[1],"S",1)==1,"sleep child ready");
        close(sleep_gate[1]);
        struct timespec duration={1,0},rem={0,0};
        errno=0;
        int rc=nanosleep(&duration,&rem);
        require(rc==-1 && errno==EINTR,"nanosleep caught EINTR");
        require(observed_usr2==1,"nanosleep handler executed");
        require(rem.tv_sec>=0 && rem.tv_nsec>=0 &&
                rem.tv_nsec<1000000000L &&
                (uint64_t)rem.tv_sec*1000000000ULL+
                (uint64_t)rem.tv_nsec<1000000000ULL,
                "nanosleep wrote bounded remaining timespec");
        _exit(0);
    }
    close(sleep_gate[1]);
    ready=0;
    require(read(sleep_gate[0],&ready,1)==1 && ready=='S',
            "nanosleep child handshake");
    close(sleep_gate[0]);
    struct timespec short_delay={0,150000000L};
    require(nanosleep(&short_delay,NULL)==0,"parent sleep before interrupt");
    require(kill(child,SIGUSR2)==0,"wake sleeping child with SIGUSR2");
    status=0;
    require(waitpid(child,&status,0)==child &&
            WIFEXITED(status) && WEXITSTATUS(status)==0,
            "nanosleep returned EINTR with remaining time");
    /* Asynchronous signal to a truly CPU-bound child: after the last
     * readiness write, the child enters a user-only spin loop. Parent
     * waits > one hardware timer quantum before kill(). The signal must
     * be delivered from an IRQ user-return path, not from child syscalls.
     * Existing M28 hardware preemption is an integral part of M30. */
    int spin_gate[2];
    require(pipe(spin_gate)==0,"CPU spin handshake pipe");
    child=fork();
    require(child>=0,"fork CPU spinner");
    if(child==0) {
        close(spin_gate[0]);
        observed_usr1=0;
        require(write(spin_gate[1],"R",1)==1,"spinner handshake");
        close(spin_gate[1]);
        while(!observed_usr1) {
            __asm__ volatile("" ::: "memory");
        }
        require(observed_info==1,"SIGINFO handler in spinning child");
        _exit(0);
    }
    close(spin_gate[1]);
    ready=0;
    require(read(spin_gate[0],&ready,1)==1 && ready=='R',
            "CPU spinner entered user execution");
    close(spin_gate[0]);
    struct timespec interval={0,100000000L};
    require(nanosleep(&interval,NULL)==0,
            "allow CPU spinner a full timer quantum");
    require(kill(child,SIGUSR1)==0,"signal CPU-bound child");
    status=0;
    require(waitpid(child,&status,0)==child &&
            WIFEXITED(status) && WEXITSTATUS(status)==0,
            "IRQ user-return delivered signal to CPU-bound process");
    puts("m30-signal-ok");
    return 0;
}
