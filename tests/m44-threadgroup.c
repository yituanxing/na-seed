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
#define NR_getpid 39
#define NR_wait4 61
#define NR_exit 60
#define NR_exit_group 231
#define NR_futex 202
#define NR_clock_nanosleep 230
#elif defined(__riscv) || defined(__aarch64__)
#define NR_read 63
#define NR_write 64
#define NR_clone 220
#define NR_getpid 172
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
#define NAS_TASK_LIMIT_STRESS 72U
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
static void short_nap(void) {
    const struct timespec64 ts={0,5000000}; /* 5ms monotonic */
    if(linux_call6(NR_clock_nanosleep,1,0,(long)&ts,0,0,0)!=0)
        done(98);
}
static long reap(long pid);
static volatile int role;
static volatile unsigned churn_exited;
static volatile long worker_fork_pid;
static volatile long sibling_wait_pid,sibling_wait_status;
static volatile unsigned sibling_wait_done;
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
    if(what==4) {
        /* Detached CLONE_THREAD death must release its Task slot AFTER
         * the native scheduler switches away from this exact C stack. */
        ++churn_exited;
        finish_thread(0);
    }
    if(what==5) {
        long child=linux_call6(NR_clone,17,0,0,0,0,0);
        if(child<0)finish_thread(94);
        if(child==0)done(0); /* new PROCESS, original process PPID */
        worker_fork_pid=child;
        finish_thread(0);
    }
    if(what==7) {
        sibling_wait_status=reap(sibling_wait_pid);
        sibling_wait_done=1;
        finish_thread(0);
    }
    if(what==6) {
        /* A nonleader of the actual PID 1 group requests process-wide
         * shutdown; this MUST be different from thread-local SYS_exit. */
        MSG("M44_PID1_GROUP_EXIT_PASS\n");
        done(0);
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
/* Linux enters ELF _start with RSP 16-byte aligned, NOT through a
 * SysV CALL (which would have pushed an 8-byte return address). This test
 * deliberately uses C as the ELF entry without crt1; tell clang that its
 * incoming stack may have either alignment. Otherwise it may compile an
 * aligned MOVAPS store to an 8-mod-16 stack location and #GP before testing
 * the kernel's actual clone/thread-group behavior. The Linux ABI and every
 * test assertion remain unchanged. */
#if defined(__x86_64__)
__attribute__((force_align_arg_pointer))
#endif
void _start(void) {
    if(call1(NR_getpid,0)==1) {
        /* Genuine direct PID 1 ELF, not a subprocess of an Alpine shell.
         * Its CLONE_THREAD children inherit PPID=0. A worker calling
         * SYS_exit must NOT be mistaken for PID 1 system shutdown; the
         * real leader must continue running, create more threads, and
         * then let a different worker call exit_group for clean shutdown.
         * Any early poweroff misses BOTH expected user markers. */
        for(unsigned k=0;k<8U;k++) {
            spawn(0,4);
            for(unsigned tries=0;churn_exited!=k+1U && tries<200U;tries++)
                short_nap();
            if(churn_exited!=k+1U)done(86);
            short_nap();
        }
        if(call1(NR_getpid,0)!=1)done(87);
        MSG("M44_PID1_WORKER_EXIT_PASS\n");
        spawn(0,6);
        for(;;)short_nap(); /* the worker must shutdown PID 1 group */
    }
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
    /* More than the 48-slot limit, serially. A leaked ZOMBIE nonleader
     * or its stale shared-file/MM owner exhausts the real Task table. */
    for(unsigned k=0;k<NAS_TASK_LIMIT_STRESS;k++) {
        unsigned want=k+1U;
        spawn(0,4);
        for(unsigned tries=0;churn_exited!=want && tries<200U;tries++)
            short_nap();
        if(churn_exited!=want)done(84);
        short_nap(); /* allow worker's actual SYS_exit + scheduler reap */
    }
    MSG("M44_THREAD_REAP_PASS\n");
    /* Fork from a nonleader: wait4 must find it under the GROUP parent.
     * The worker can exit before its child; no dangling worker-TID PPID. */
    worker_fork_pid=0;
    spawn(0,5);
    for(unsigned tries=0;!worker_fork_pid && tries<200U;tries++)
        short_nap();
    if(worker_fork_pid<=0 || reap(worker_fork_pid)!=0)done(85);
    MSG("M44_WORKER_FORK_PARENT_PASS\n");
    /* Reverse ownership: group leader forks and a sibling TID blocks in
     * wait4; it must observe, wake and reap that PROCESS child. */
    sibling_wait_done=0;sibling_wait_status=-1;
    long sibling_child=fork_child();
    if(sibling_child<0)done(88);
    if(sibling_child==0){short_nap();short_nap();done(0);}
    sibling_wait_pid=sibling_child;
    spawn(0,7);
    for(unsigned tries=0;!sibling_wait_done && tries<200U;tries++)short_nap();
    if(!sibling_wait_done || sibling_wait_status!=0)done(89);
    MSG("M44_SIBLING_WAIT4_PASS\n");
    MSG("M44_THREAD_GROUP_PASS\n");
    done(0);
}
