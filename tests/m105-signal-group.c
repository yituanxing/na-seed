#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Real three-ISA guest: ordinary Linux terminal/session/fork/sigaction ABI,
 * plus musl's exact supported CLONE_THREAD mask using a naked trampoline.
 * Workers use only raw syscalls (never glibc without TLS). Every test hits
 * the production scheduler/TTY/signal implementation in a native QEMU VM. */
#if defined(__x86_64__)
#define NAS_N_EXIT 60
#define NAS_N_GETPID 39
#define NAS_N_TGKILL 234
#define NAS_N_SLEEP 230
#elif defined(__riscv) || defined(__aarch64__)
#define NAS_N_EXIT 93
#define NAS_N_GETPID 172
#define NAS_N_TGKILL 131
#define NAS_N_SLEEP 115
#else
#error "M105 supports only x86-64, RV64, AArch64"
#endif
#define NAS_MUSL_THREAD_FLAGS 0x7d0f00
#define NAS_WORKER_COUNT 2

static volatile unsigned worker_started,worker_release,handler_count;
static volatile unsigned worker_tid[NAS_WORKER_COUNT],worker_clear[NAS_WORKER_COUNT];
static _Alignas(16) unsigned char worker_stacks[NAS_WORKER_COUNT][65536];

static void ck(int ok,const char *why) {
    if(ok)return;
    fprintf(stderr,"[M105 FAIL] %s errno=%d\n",why,errno);
    fflush(stderr);
    _exit(105);
}
static void mark(const char *line) {puts(line);fflush(stdout);}
static void nap(unsigned ms) {
    struct timespec ts={(time_t)(ms/1000U),(long)((ms%1000U)*1000000U)};
    while(nanosleep(&ts,&ts)<0 && errno==EINTR) { }
}
static void raw_thread_exit(void) __attribute__((noreturn));
static void raw_thread_exit(void) {
#if defined(__x86_64__)
    __asm__ volatile("syscall" : : "a"(NAS_N_EXIT),"D"(0) : "rcx","r11","memory");
#elif defined(__riscv)
    register long a0 __asm__("a0")=0;
    register long a7 __asm__("a7")=NAS_N_EXIT;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
#else
    register long x0 __asm__("x0")=0;
    register long x8 __asm__("x8")=NAS_N_EXIT;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
#endif
    for(;;) { }
}
static void raw_pause(void) {
    struct {long sec,nsec;} request={0,20000000L};
#if defined(__x86_64__)
    register long r10 __asm__("r10")=0;
    __asm__ volatile("syscall" : : "a"(NAS_N_SLEEP),"D"(1),"S"(0),
                     "d"((long)&request),"r"(r10) : "rcx","r11","memory");
#elif defined(__riscv)
    register long a0 __asm__("a0")=1;
    register long a1 __asm__("a1")=0;
    register long a2 __asm__("a2")=(long)&request;
    register long a7 __asm__("a7")=NAS_N_SLEEP;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1),"r"(a2),"r"(a7) : "memory");
#else
    register long x0 __asm__("x0")=1;
    register long x1 __asm__("x1")=0;
    register long x2 __asm__("x2")=(long)&request;
    register long x8 __asm__("x8")=NAS_N_SLEEP;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x8) : "memory");
#endif
}
/* CLONE_SETTLS installs a null thread pointer in this deliberately libc-free
 * worker.  With a distro's default -fstack-protector-strong, GCC would read
 * the canary from that absent TLS before even incrementing worker_started.
 * Disable only this raw-syscall trampoline target; parent code retains its
 * normal stack-protector policy. */
static __attribute__((noreturn,used,no_stack_protector)) void worker_main(void) {
    __atomic_add_fetch(&worker_started,1,__ATOMIC_SEQ_CST);
    while(!worker_release)raw_pause();
    raw_thread_exit();
}
/* The child must jump into a freshly allocated stack, not `ret` through
 * its parent's missing C frames. This is the same real ABI as M103. */
#if defined(__aarch64__)
/* GCC for AArch64 does not implement naked C functions: the attribute is
 * ignored (and -Werror correctly rejects it). This genuine ELF assembly
 * symbol must have NO compiler-generated prologue before clone switches
 * stacks. x0=stack, x1=parent_tid, x2=child_clear_tid at entry. */
extern long launch_worker(void *stack,volatile unsigned *ptid,
                          volatile unsigned *ctid);
__asm__(
    ".text\n.balign 4\n.global launch_worker\n"
    ".type launch_worker,%function\n"
    "launch_worker:\n"
    "mov x4,x2\nmov x2,x1\nmov x1,x0\nmov x3,xzr\n"
    "movz x0,#0x0f00\nmovk x0,#0x7d,lsl #16\n"
    "mov x8,#220\nsvc #0\ncbz x0,1f\nret\n"
    "1: b worker_main\n"
    ".size launch_worker,.-launch_worker\n");
#else
__attribute__((naked)) static long launch_worker(
                 void *stack __attribute__((unused)),
                 volatile unsigned *ptid __attribute__((unused)),
                 volatile unsigned *ctid __attribute__((unused))) {
#if defined(__x86_64__)
    __asm__ volatile(
        "mov %rdx,%r10\nmov %rsi,%rdx\nmov %rdi,%rsi\n"
        "mov $0x7d0f00,%edi\nxor %r8d,%r8d\nmov $56,%eax\n"
        "syscall\ntest %rax,%rax\njz 1f\nret\n"
        "1: sub $8,%rsp\njmp worker_main\n");
#elif defined(__riscv)
    __asm__ volatile(
        "mv a4,a2\nmv a2,a1\nmv a1,a0\nli a3,0\n"
        "li a0,0x7d0f00\nli a7,220\necall\n"
        "beqz a0,1f\nret\n1: tail worker_main\n");
#endif
}
#endif
static void make_workers(unsigned count) {
    ck(count>0 && count<=NAS_WORKER_COUNT,"worker count");
    worker_started=worker_release=0;
    for(unsigned i=0;i<count;i++) {
        worker_tid[i]=0;
        worker_clear[i]=0x13572468U;
        void *sp=worker_stacks[i]+sizeof(worker_stacks[i]);
        long tid=launch_worker(sp,&worker_tid[i],&worker_clear[i]);
        ck(tid>0 && (unsigned long)tid==worker_tid[i],
           "real musl-style clone returned valid parent TID");
    }
    for(unsigned tries=0;worker_started<count && tries<300;tries++)nap(10);
    ck(worker_started==count,"all genuine worker TIDs scheduled");
}
static void finish_workers(unsigned count) {
    worker_release=1;
    pid_t tgid=getpid();
    for(unsigned i=0;i<count;i++) {
        int gone=0;
        for(unsigned tries=0;tries<300;tries++) {
            errno=0;
            if(syscall(NAS_N_TGKILL,tgid,worker_tid[i],0)<0 && errno==ESRCH) {
                gone=1;break;
            }
            nap(10);
        }
        ck(gone,"all workers exited before stack reuse");
    }
}
static void send_ready(int fd,char status) {
    ck(write(fd,&status,1)==1,"write readiness token");
}
static void wait_ready(int fd,char status) {
    char got=0;
    ck(read(fd,&got,1)==1 && got==status,"read exact readiness token");
}
static void wait_signal(pid_t pid,int signum) {
    int status=0;
    ck(waitpid(pid,&status,0)==pid,"wait real thread-group child");
    ck(WIFSIGNALED(status) && WTERMSIG(status)==signum,
       "actual wait4 signal termination status");
}
static void handle_quit(int signo) {
    if(signo!=SIGQUIT)raw_thread_exit();
    __atomic_add_fetch(&handler_count,1,__ATOMIC_SEQ_CST);
}
static void child_default(int ready_fd,int ttyfd,unsigned count,char token) {
    (void)ttyfd;
    ck(setpgid(0,0)==0,"foreground leader setpgid");
    make_workers(count);
    send_ready(ready_fd,token);
    /* Never read the controlling terminal while still background: that
     * would legitimately stop on SIGTTIN before the parent can set PGID.
     * Timer-driven user/syscall returns keep the entire group live. */
    for(;;)nap(50);
}
static void child_caught(int ready_fd,unsigned count) {
    ck(setpgid(0,0)==0,"caught leader setpgid");
    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler=handle_quit;
    ck(sigaction(SIGQUIT,&sa,NULL)==0,"install real caught SIGQUIT");
    handler_count=0;
    make_workers(count);
    send_ready(ready_fd,'H');
    for(unsigned tries=0;handler_count==0 && tries<500;tries++)nap(10);
    ck(handler_count==1,"SIGQUIT delivered exactly once initially");
    /* Give every worker genuine additional syscall/user-return boundaries;
     * a buggy per-TID broadcast would invoke more than one handler. */
    nap(200);
    ck(handler_count==1,"one process-directed SIGQUIT, not per-TID fanout");
    finish_workers(count);
    _exit(0);
}
static int tty_setup(void) {
    ck(ioctl(0,TIOCSCTTY,0)==0,"attach controlling physical terminal");
    int ttyfd=open("/dev/tty",O_RDWR);
    ck(ttyfd>=0,"open physical controlling terminal");
    struct termios term;
    ck(tcgetattr(ttyfd,&term)==0,"read termios");
    term.c_iflag|=ICRNL;
    term.c_lflag|=ISIG|ICANON|ECHO;
    ck(tcsetattr(ttyfd,TCSANOW,&term)==0,"enable ISIG canonical mode");
    return ttyfd;
}
static void stage_default_sigint(int ttyfd) {
    int pipefd[2];ck(pipe(pipefd)==0,"default readiness pipe");
    pid_t leader=fork();ck(leader>=0,"fork default thread-group leader");
    if(!leader) {
        close(pipefd[0]);
        child_default(pipefd[1],ttyfd,2,'A');
        _exit(95);
    }
    close(pipefd[1]);
    wait_ready(pipefd[0],'A');close(pipefd[0]);
    ck(tcsetpgrp(ttyfd,leader)==0,"foreground default group");
    mark("[M105] default-parent-ready");
    wait_signal(leader,SIGINT);
    ck(tcsetpgrp(ttyfd,getpgrp())==0,"restore foreground after SIGINT");
    mark("M105_TTY_DEFAULT_SIGINT_PASS");
}
static void stage_mixed_sigquit(int ttyfd) {
    int ready[2],go[2];
    ck(pipe(ready)==0 && pipe(go)==0,"mixed-group pipes");
    pid_t caught=fork();ck(caught>=0,"fork handler TGID");
    if(!caught) {
        close(ready[0]);close(go[0]);close(go[1]);
        child_caught(ready[1],2);
    }
    close(go[0]);
    wait_ready(ready[0],'H');
    pid_t victim=fork();ck(victim>=0,"fork other default TGID");
    if(!victim) {
        close(ready[0]);close(go[1]);
        ck(setpgid(0,caught)==0,"join caught group PGID with own TGID");
        send_ready(ready[1],'V');
        for(;;)nap(50);
    }
    close(ready[1]);wait_ready(ready[0],'V');close(ready[0]);
    close(go[1]);
    ck(getpgid(caught)==caught && getpgid(victim)==caught,
       "two independent TGIDs in one foreground PGID");
    ck(tcsetpgrp(ttyfd,caught)==0,"mixed foreground PGID");
    mark("[M105] caught-parent-ready");
    wait_signal(victim,SIGQUIT);
    int status=0;
    ck(waitpid(caught,&status,0)==caught && WIFEXITED(status) &&
       WEXITSTATUS(status)==0,"caught TGID survived with one handler");
    ck(tcsetpgrp(ttyfd,getpgrp())==0,"restore foreground after SIGQUIT");
    mark("M105_TTY_HANDLER_ONCE_PASS");
}
static void stage_zombie_leader(int signum,int via_pgid) {
    int ready[2];ck(pipe(ready)==0,"zombie leader readiness pipe");
    pid_t leader=fork();ck(leader>=0,"fork early SYS_exit leader");
    if(!leader) {
        close(ready[0]);
        ck(setpgid(0,0)==0,"zombie leader own PGID");
        make_workers(1);
        send_ready(ready[1],'Z');
        raw_thread_exit(); /* only the leader exits, worker stays alive */
    }
    close(ready[1]);wait_ready(ready[0],'Z');close(ready[0]);
    nap(100); /* allow leader SYS_exit; worker remains in raw sleep loop */
    int status=0;
    ck(waitpid(leader,&status,WNOHANG)==0,"leader death deferred for worker");
    pid_t dest=via_pgid ? -leader : leader;
    ck(kill(dest,signum)==0,"kill zombie-leader process with live worker");
    wait_signal(leader,signum);
}
static void stage_init_worker_protection(int ttyfd) {
    make_workers(1);
    ck(tcsetpgrp(ttyfd,getpgrp())==0,"PID1 foreground for immunity test");
    mark("[M105] init-parent-ready");
    /* ISIG is deferred to the privileged scheduler. A ^C may not kill
     * either PID1 or its CLONE_THREAD worker (tgid=1, tid!=1). */
    nap(400);
    errno=0;
    ck(syscall(NAS_N_TGKILL,getpid(),worker_tid[0],0)==0,
       "protected PID1 worker survived TTY SIGINT");
    finish_workers(1);
    mark("M105_TTY_PID1_WORKER_PASS");
}
int main(int argc,char **argv) {
    /* The portable Linux host oracle exercises the process-group and
     * zombie-leader cases. Only a real NaSeed PID 1 can enforce the
     * intentionally protected init-worker terminal policy. */
    if(argc==2 && strcmp(argv[1],"--linux-oracle")==0) {
        stage_zombie_leader(SIGTERM,0);
        stage_zombie_leader(SIGINT,1);
        mark("M105_LINUX_ORACLE_PASS");
        return 0;
    }
    ck(argc==1,"boot as native PID1");
    int ttyfd=tty_setup();
    stage_default_sigint(ttyfd);
    stage_mixed_sigquit(ttyfd);
    stage_zombie_leader(SIGTERM,0);
    stage_zombie_leader(SIGINT,1);
    mark("M105_ZOMBIE_LEADER_KILL_PASS");
    stage_init_worker_protection(ttyfd);
    close(ttyfd);
    mark("M105_SIGNAL_GROUP_NATIVE_PASS");
    return 0;
}
