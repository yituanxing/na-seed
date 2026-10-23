/* M46 direct-Linux64 syscall-only stress, host LLVM, 3 native ISAs. */
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
#if defined(__x86_64__)
#define NR_write 1
#define NR_clone 56
#define NR_exit_group 231
#define NR_execve 59
#define NR_wait4 61
#define NR_futex 202
#define NR_sleep 230
#else
#define NR_write 64
#define NR_clone 220
#define NR_exit_group 94
#define NR_execve 221
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
struct ts64 {s64 sec,nsec;};
static void nap(void) {
    const struct ts64 ts={0,300000000};
    if(sys6(NR_sleep,1,0,(long)&ts,0,0,0)!=0)exitall(91);
}
static volatile u32 clear_tid;
static volatile u32 blockword;
static volatile int role;
static __attribute__((aligned(16))) unsigned char stack[65536];
static char *const argv1[]={"/tests/m46-exec-target",0};
static char *const envp1[]={0};
static __attribute__((noreturn)) void replace(void) {
    sys3(NR_execve,(long)argv1[0],(long)argv1,(long)envp1);
    exitall(92); /* Successful execve does not return. */
}
static __attribute__((used,noreturn)) void worker_entry(void) {
    /* Nonleader calls exec while leader is blocked. A failed exec
     * must NOT kill sibling or detach the CLONE_FILES/old VM. */
    if(role==2) {
        long bad=sys3(NR_execve,(long)"/tests/no-such-elf",0,0);
        if(bad!=-2)exitall(93);
        nap(); /* Let group leader enter futex wait on old VM. */
        replace();
    }
    /* Leader exec: park worker in futex first. */
    (void)sys3(NR_futex,(long)&blockword,128,0);
    exitall(94);
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
static void one_child(int which) {
    role=which;
    long tid=spawn((void*)(stack+sizeof(stack)),&clear_tid,&clear_tid);
    if(tid<=0)exitall(95);
    if(which==1){
        /* Current task after clone returned to leader. Its worker is
         * parked on futex. Commit the ELF from the leader. */
        replace();
    }
    /* Current group leader is still in old VM until worker exec. */
    (void)sys3(NR_futex,(long)&blockword,128,0);
    exitall(96); /* must not survive worker exec */
}
static __attribute__((noreturn)) void stress_main(void) {
    for(int which=1;which<=2;which++){
        long child=sys6(NR_clone,17,0,0,0,0,0);
        if(child<0)exitall(97);
        if(child==0)one_child(which);
        int status=-1;
        long got=sys6(NR_wait4,child,(long)&status,0,0,0,0);
        if(got!=child || status!=0)exitall(98);
        if(which==1)SAY("M46_LEADER_EXEC_WAIT4_PASS\n");
        else SAY("M46_WORKER_EXEC_TGID_WAIT4_PASS\n");
    }
    SAY("M46_MULTITHREAD_EXEC_PASS\n");
    exitall(0);
}
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall stress_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void){stress_main();}
#endif
