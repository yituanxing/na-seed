/* M44: freestanding, real three-ISA Linux syscall group-lifetime gate.
 * Built with the host's upstream LLVM; runs only in native NaSeed QEMU.
 * No fake libc, custom pthread shim or special kernel dispatch paths. */
/* Freestanding LP64: no cross-libc headers required for raw syscalls. */
typedef unsigned int uint32_t;
typedef long int64_t;
#if defined(__x86_64__)
#define NR_read 0
#define NR_write 1
#define NR_clone 56
#define NR_wait4 61
#define NR_exit 60
#define NR_exit_group 231
#define NR_futex 202
#define NR_clock_nanosleep 230
#elif defined(__riscv) || defined(__aarch64__)
#define NR_read 63
#define NR_write 64
#define NR_clone 220
#define NR_wait4 260
#define NR_exit 93
#define NR_exit_group 94
#define NR_futex 98
#define NR_clock_nanosleep 115
#else
#error "M44 requires one of the three supported native Linux64 targets"
#endif
static inline long linux_call6(long n,long a,long b,long c,long d,long e,long f) {
#if defined(__x86_64__)
    register long r10 __asm__("r10")=d;
    register long r8 __asm__("r8")=e;
    register long r9 __asm__("r9")=f;
    long v;
    __asm__ volatile("syscall":"=a"(v):"a"(n),"D"(a),"S"(b),"d"(c),
                     "r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");
    return v;
#elif defined(__riscv)
    register long x0 __asm__("a0")=a;
    register long x1 __asm__("a1")=b;
    register long x2 __asm__("a2")=c;
    register long x3 __asm__("a3")=d;
    register long x4 __asm__("a4")=e;
    register long x5 __asm__("a5")=f;
    register long x7 __asm__("a7")=n;
    __asm__ volatile("ecall":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),
                     "r"(x4),"r"(x5),"r"(x7):"memory");
    return x0;
#else
    register long x0 __asm__("x0")=a;
    register long x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c;
    register long x3 __asm__("x3")=d;
    register long x4 __asm__("x4")=e;
    register long x5 __asm__("x5")=f;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3),
                     "r"(x4),"r"(x5),"r"(x8):"memory");
    return x0;
#endif
}
static inline long call3(long n,long a,long b,long c) {
    return linux_call6(n,a,b,c,0,0,0);
}
static inline long call1(long n,long a) {
    return linux_call6(n,a,0,0,0,0,0);
}
#define MSG(s) (void)call3(NR_write,1,(long)(s),sizeof(s)-1)
static __attribute__((noreturn)) void done(int n) {
    call1(NR_exit_group,n);
    for(;;){}
}
static __attribute__((noreturn)) void finish_thread(int n) {
    call1(NR_exit,n);
    for(;;){}
}
struct timespec64 { int64_t sec,nsec; };
static void nap(void) {
    /* Hold a genuine timed syscall-blocked worker until the leader exits.
     * A short 20ms pause can expire before native ARM64 leader SYS_exit,
     * leaving the thread-group deferred-lifetime branch untested. */
    const struct timespec64 ts={1,0};
    if (linux_call6(NR_clock_nanosleep,1,0,(long)&ts,0,0,0)!=0)
        done(97); /* A missing sleep syscall must NEVER fake this race. */
}
static volatile int role;
static uint32_t ids[2];
static __attribute__((aligned(16))) unsigned char stacks[2][65536];
static __attribute__((used,noreturn)) void thread_entry(void) {
    int what=role;
    if(what==1) {
        static volatile int lockword;
        (void)linux_call6(NR_futex,(long)&lockword,0,0,0,0,0);
        done(95); /* Must never outlive worker exit_group. */
    }
    if(what==2) done(37); /* A nonleader kills the entire thread group. */
    if(what==3) {
        nap(); /* Leader must exit(23) and keep our shared FD usable. */
        MSG("M44_WORKER_SURVIVED_LEADER\n");
        finish_thread(0); /* Last worker finally makes leader waitable. */
    }
    done(96);
}
/* clone restarts at the next instruction but switches SP, so do not
 * return into a missing C caller frame on the CHILD stack. */
__attribute__((naked)) static long launch_thread(void *sp __attribute__((unused)),
                                                  uint32_t *ptid __attribute__((unused)),
                                                  uint32_t *ctid __attribute__((unused))) {
#if defined(__x86_64__)
    __asm__ volatile(
        "mov %rdx, %r10\n"
        "mov %rsi, %rdx\n"
        "mov %rdi, %rsi\n"
        "mov $0x7d0f00, %edi\n"
        "xor %r8d, %r8d\n"
        "mov $56, %eax\n"
        "syscall\n"
        "test %rax, %rax\n"
        "jz 1f\n"
        "ret\n"
        /* Linux clone supplies a 16-aligned stack. The x86-64 SysV ABI
         * requires RSP=8(mod16) on entry to a C function. A JMP rather
         * than CALL does not push a return address; reserve it here or
         * Clang's aligned SSE stack moves can trigger #GP. */
        "1: sub $8, %rsp\n"
        "jmp thread_entry\n");
#elif defined(__riscv)
    __asm__ volatile(
        "mv a4,a2\n"
        "mv a2,a1\n"
        "mv a1,a0\n"
        "li a3,0\n"
        "li a0,0x7d0f00\n"
        "li a7,220\n"
        "ecall\n"
        "beqz a0,1f\n"
        "ret\n"
        "1: tail thread_entry\n");
#else
    __asm__ volatile(
        "mov x4,x2\n"
        "mov x2,x1\n"
        "mov x1,x0\n"
        "mov x3,xzr\n"
        "movz x0,#0x0f00\n"
        "movk x0,#0x7d,lsl #16\n"
        "mov x8,#220\n"
        "svc #0\n"
        "cbz x0,1f\n"
        "ret\n"
        "1: b thread_entry\n");
#endif
}
static void spawn(int slot,int which) {
    role=which;
    long sp=(long)(stacks[slot]+sizeof(stacks[slot]));
    if(launch_thread((void*)sp,&ids[slot],&ids[slot])<=0)done(91);
}
static long fork_child(void) {
    return linux_call6(NR_clone,17,0,0,0,0,0);
}
static long reap(long pid) {
    int status=-1;
    long got=linux_call6(NR_wait4,pid,(long)&status,0,0,0,0);
    if(got!=pid)return -1;
    return status;
}
void _start(void) {
    /* Three rounds detect zombie TID slots, shared fd over-release, and
     * premature freeing of the leader's shared page-table root. */
    for(int k=0;k<3;k++) {
        long pid=fork_child();
        if(pid<0)done(80);
        if(pid==0) {
            spawn(0,1); /* Parks on a real futex in sibling. */
            spawn(1,2); /* Calls exit_group while leader is alive. */
            for(;;)nap();
        }
        if(reap(pid)!=(37<<8))done(81);
    }
    MSG("M44_WORKER_EXIT_GROUP_PASS\n");
    long pid=fork_child();
    if(pid<0)done(82);
    if(pid==0) {
        spawn(0,3);
        /* SYS_exit is thread-local, not exit_group. The child must remain
         * visible to its original parent only AFTER the last thread exit. */
        finish_thread(23);
    }
    if(reap(pid)!=(23<<8))done(83);
    MSG("M44_LEADER_DEFER_PASS\n");
    MSG("M44_THREAD_GROUP_PASS\n");
    done(0);
}
