/* M47: genuine thread-directed Linux64 signal routing, 3 native ISAs. */
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
#if defined(__x86_64__)
#define NR_write 1
#define NR_clone 56
#define NR_exit_group 231
#define NR_tkill 200
#define NR_tgkill 234
#define NR_sigpending 127
#define NR_sigprocmask 14
#define NR_getpid 39
#define NR_exit 60
#define NR_wait4 61
#define NR_futex 202
#define NR_sleep 230
#else
#define NR_write 64
#define NR_clone 220
#define NR_exit_group 94
#define NR_tkill 130
#define NR_tgkill 131
#define NR_sigpending 136
#define NR_sigprocmask 135
#define NR_getpid 172
#define NR_exit 93
#define NR_wait4 260
#define NR_futex 98
#define NR_sleep 115
#endif
static inline long sys6(long n,long a,long b,long c,long d,long e,long f) {
#if defined(__x86_64__)
    register long r10 __asm__("r10")=d;
    register long r8 __asm__("r8")=e;
    register long r9 __asm__("r9")=f;
    long r;
    __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),
       "r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");
    return r;
#elif defined(__riscv)
    register long a0 __asm__("a0")=a,a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c,a3 __asm__("a3")=d;
    register long a4 __asm__("a4")=e,a5 __asm__("a5")=f;
    register long a7 __asm__("a7")=n;
    __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),
          "r"(a4),"r"(a5),"r"(a7):"memory");
    return a0;
#else
    register long x0 __asm__("x0")=a,x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c,x3 __asm__("x3")=d;
    register long x4 __asm__("x4")=e,x5 __asm__("x5")=f;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),
          "r"(x4),"r"(x5),"r"(x8):"memory");
    return x0;
#endif
}
static inline long sys3(long n,long a,long b,long c){return sys6(n,a,b,c,0,0,0);}
static inline long sys1(long n,long a){return sys6(n,a,0,0,0,0,0);}
#define SAY(s) (void)sys3(NR_write,1,(long)(s),sizeof(s)-1)
static __attribute__((noreturn)) void exitall(int n){
    sys1(NR_exit_group,n);for(;;){}
}
static volatile u32 clear_tid,blockword;
static volatile int role;
static __attribute__((aligned(16))) unsigned char stack[65536];
static __attribute__((used,noreturn)) void worker_entry(void) {
    if(role==1) {
        const u64 sigmask=1ULL<<(10-1); /* SIGUSR1 */
        if(sys6(NR_sigprocmask,0,(long)&sigmask,0,8,0,0))exitall(71);
        if(sys3(NR_futex,(long)&blockword,128,0)!=0)exitall(72);
        u64 pending=0;
        if(sys3(NR_sigpending,(long)&pending,8,0)!=0 ||
           (pending & sigmask)!=sigmask)exitall(73);
        SAY("M47_WORKER_DIRECTED_PENDING_PASS\n");
        sys1(NR_exit,0);
        for(;;){}
    }
    (void)sys3(NR_futex,(long)&blockword,128,0);
    exitall(74); /* must not run after tgkill(SIGKILL) group termination */
}
__attribute__((naked)) static long spawn(void *sp __attribute__((unused)),
                volatile u32 *parent_tid __attribute__((unused)),
                volatile u32 *child_tid __attribute__((unused))) {
#if defined(__x86_64__)
    __asm__ volatile(
      "mov %rdx,%r10\nmov %rsi,%rdx\nmov %rdi,%rsi\n"
      "mov $0x7d0f00,%edi\nxor %r8d,%r8d\nmov $56,%eax\n"
      "syscall\ntest %rax,%rax\njz 1f\nret\n"
      "1: sub $8,%rsp\njmp worker_entry\n");
#elif defined(__riscv)
    __asm__ volatile(
      "mv a4,a2\nmv a2,a1\nmv a1,a0\nli a3,0\n"
      "li a0,0x7d0f00\nli a7,220\necall\nbeqz a0,1f\n"
      "ret\n1: tail worker_entry\n");
#else
    __asm__ volatile(
      "mov x4,x2\nmov x2,x1\nmov x1,x0\nmov x3,xzr\n"
      "movz x0,#0x0f00\nmovk x0,#0x7d,lsl #16\n"
      "mov x8,#220\nsvc #0\ncbz x0,1f\nret\n"
      "1: b worker_entry\n");
#endif
}
static __attribute__((noreturn)) void scenario(int mode) {
    clear_tid=0;blockword=0;role=mode;
    long tid=spawn((void*)(stack+sizeof(stack)),&clear_tid,&clear_tid);
    if(tid<=0 || (u32)tid!=clear_tid)exitall(80);
    long group=sys1(NR_getpid,0);
    if(group<=0)exitall(81);
    if(sys3(NR_tgkill,group,tid,0)!=0 ||
       sys3(NR_tgkill,group+999,tid,0)!=-3 ||
       sys3(NR_tgkill,0,tid,0)!=-22 ||
       sys3(NR_tgkill,group,tid,65)!=-22 ||
       sys3(NR_tkill,tid,0,0)!=0)exitall(82);
    if(mode==1) {
        if(sys3(NR_tgkill,group,tid,10)!=0)exitall(83);
        u64 leader_pending=0;
        if(sys3(NR_sigpending,(long)&leader_pending,8,0)!=0 ||
           leader_pending!=0)exitall(84);
        blockword=1;
        if(sys3(NR_futex,(long)&blockword,129,1)!=1)exitall(85);
        /* Real CLONE_CHILD_CLEARTID wake, never an invented delay. */
        if(clear_tid && sys3(NR_futex,(long)&clear_tid,128,(long)tid)!=0)
            exitall(86);
        if(clear_tid!=0 ||
           sys3(NR_tgkill,group,tid,0)!=-3)exitall(87);
        SAY("M47_TGID_TID_VALIDATION_PASS\n");
        exitall(0);
    }
    /* Fatal *thread-directed* SIGKILL must destroy the entire process,
     * preserve WIFSIGNALED=9 for its parent, and reclaim parked worker. */
    (void)sys3(NR_tgkill,group,tid,9);
    exitall(88); /* fatal action must not return to caller */
}
static __attribute__((used,noreturn)) void stress_main(void) {
    for(int mode=1;mode<=2;mode++) {
        long child=sys6(NR_clone,17,0,0,0,0,0);
        if(child<0)exitall(89);
        if(child==0)scenario(mode);
        int status=-1;
        long got=sys6(NR_wait4,child,(long)&status,0,0,0,0);
        if(got!=child || (mode==1?status!=0:status!=9))exitall(90);
        if(mode==1)SAY("M47_TARGETED_MASKED_WAIT_PASS\n");
        else SAY("M47_GROUP_FATAL_WAIT4_PASS\n");
    }
    SAY("M47_THREAD_SIGNALS_PASS\n");
    exitall(0);
}
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall stress_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void){stress_main();}
#endif
