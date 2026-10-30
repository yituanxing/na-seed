/* One freestanding, genuine Linux syscall workload for real NaSeed/QEMU on
 * x86-64, RV64 and AArch64. No libc stubs or fake test-only kernel ops.
 * The same source/binary ABI is also runnable on target Linux for an oracle. */
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
#if defined(__x86_64__)
#define N_CLONE 56
#define N_EXIT 60
#define N_EXIT_GROUP 231
#define N_GETTID 186
#define N_GETPID 39
#define N_TGKILL 234
#define N_FUTEX 202
#define N_SET_TID_ADDRESS 218
#define N_SET_ROBUST 273
#define N_SLEEP 230
#define N_WAIT4 61
#define N_WRITE 1
#elif defined(__riscv) || defined(__aarch64__)
#define N_CLONE 220
#define N_EXIT 93
#define N_EXIT_GROUP 94
#define N_GETTID 178
#define N_GETPID 172
#define N_TGKILL 131
#define N_FUTEX 98
#define N_SET_TID_ADDRESS 96
#define N_SET_ROBUST 99
#define N_SLEEP 115
#define N_WAIT4 260
#define N_WRITE 64
#else
#error "NaSeed M103 guest needs x86-64, RV64 or AArch64"
#endif
static inline long sys6(long n,long a,long b,long c,long d,long e,long f) {
#if defined(__x86_64__)
    register long r10 __asm__("r10")=d;
    register long r8 __asm__("r8")=e;
    register long r9 __asm__("r9")=f;
    long ret;
    __asm__ volatile("syscall":"=a"(ret):"a"(n),"D"(a),"S"(b),"d"(c),
                     "r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");
    return ret;
#elif defined(__riscv)
    register long a0 __asm__("a0")=a;
    register long a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c;
    register long a3 __asm__("a3")=d;
    register long a4 __asm__("a4")=e;
    register long a5 __asm__("a5")=f;
    register long a7 __asm__("a7")=n;
    __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a3),
                     "r"(a4),"r"(a5),"r"(a7):"memory");
    return a0;
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
#define SYS1(n,a) sys6(n,a,0,0,0,0,0)
#define SYS3(n,a,b,c) sys6(n,a,b,c,0,0,0)
#define SAY(s) (void)SYS3(N_WRITE,1,(long)(s),sizeof(s)-1)
#define CLEAR_MARKER 0x13572468U
#define OWNER_DIED 0x40000000U
static __attribute__((noreturn)) void fail(unsigned code) {
    SYS1(N_EXIT_GROUP,(long)code);
    for(;;){}
}
static __attribute__((noreturn)) void leave_thread(void) {
    SYS1(N_EXIT,0);
    for(;;){}
}
struct ts64 {s64 sec,nsec;};
static void nap_ns(s64 ns) {
    struct ts64 t={0,ns};
    if(sys6(N_SLEEP,1,0,(long)&t,0,0,0))fail(90);
}
static void wait_flag(volatile u32 *word) {
    for(unsigned tries=0;!(*word) && tries<1000;tries++) {
        long w=SYS3(N_FUTEX,(long)word,128,0);
        if(w!=0 && w!=-11)fail(91);
    }
    if(!(*word))fail(92);
}
/* Child announces worker_ready BEFORE its final SYS_exit. Merely sleeping
 * for 5ms after that flag is NOT proof that its private user stack may be
 * reused. A signal-zero TGKILL sees ESRCH only after native Task exit.
 * This bounded handshake is valid in both Linux and NaSeed on all ISAs. */
static void wait_thread_gone(long tid) {
    long tgid=SYS1(N_GETPID,0);
    if(tgid<=0)fail(93);
    for(unsigned tries=0;tries<400;tries++) {
        long ret=SYS3(N_TGKILL,tgid,tid,0);
        if(ret==-3)return; /* ESRCH: worker no longer live */
        if(ret!=0)fail(94);
        nap_ns(5000000);
    }
    fail(95);
}
static volatile u32 fork_tid=CLEAR_MARKER;
static void test_process_child_tid(long address,int valid) {
    /* On RV/ARM clone's child_tid is argument a4; on x86 it is r10.
     * The return value is not invalidated by a child-side put_user fault. */
#if defined(__x86_64__)
    long pid=sys6(N_CLONE,0x01200011,0,0,address,0,0);
#else
    long pid=sys6(N_CLONE,0x01200011,0,0,0,address,0);
#endif
    if(pid<0)fail(11);
    if(!pid) {
        long tid=SYS1(N_GETTID,0);
        if(valid && fork_tid!=(u32)tid)fail(12);
        /* This supersedes the clone-supplied pointer for exit cleanup.
         * Ordinary user faults must never halt the parent or the kernel. */
        if(SYS1(N_SET_TID_ADDRESS,1)!=tid)fail(13);
        fail(0);
    }
    int status=0;
    if(sys6(N_WAIT4,pid,(long)&status,0,0,0,0)!=pid || status!=0)
        fail(14);
    if(fork_tid!=CLEAR_MARKER)fail(15); /* child's COW TID, not parent's */
}

struct robust_head {u64 next;s64 offset;u64 pending;};
struct robust_node {u64 next;volatile u32 futex;u32 pad;};
static struct robust_head pending_head;
static struct robust_node pending_node;
static volatile u32 tid_parent,tid_clear,worker_ready,worker_release;
static volatile u32 worker_error,worker_role;
static __attribute__((aligned(16))) unsigned char worker_stack[65536];
static __attribute__((used,noreturn)) void worker_entry(void) {
    long tid=SYS1(N_GETTID,0);
    if(tid<=0 || tid_clear!=CLEAR_MARKER)worker_error=31;
    if(worker_role==1) {
        /* musl pthread clone flags request PARENT_SETTID and CLEAR, but
         * NOT CHILD_SETTID: this value remains untouched until exit. */
        worker_ready=1;
        (void)SYS3(N_FUTEX,(long)&worker_ready,129,1);
        leave_thread();
    }
    if(worker_role==2) {
        if(SYS1(N_SET_TID_ADDRESS,1)!=tid)worker_error=32;
        worker_ready=1;
        (void)SYS3(N_FUTEX,(long)&worker_ready,129,1);
        leave_thread();
    }
    if(worker_role==3) {
        /* A genuine robust pending unlock with owner=0 must wake a
         * FUTEX_WAIT on the same shared word at worker exit. */
        pending_head.next=(u64)&pending_head.next;
        pending_head.offset=8;
        pending_head.pending=(u64)&pending_node.next;
        pending_node.next=(u64)&pending_head.next;
        pending_node.futex=0;
        if(sys6(N_SET_ROBUST,(long)&pending_head,24,0,0,0,0))
            worker_error=33;
        worker_ready=1;
        (void)SYS3(N_FUTEX,(long)&worker_ready,129,1);
        while(!worker_release) {
            long w=SYS3(N_FUTEX,(long)&worker_release,128,0);
            if(w!=0 && w!=-11)worker_error=34;
        }
        nap_ns(250000000); /* parent arms FUTEX_WAIT before owner dies */
        leave_thread();
    }
    worker_error=35;
    leave_thread();
}
/* A freshly cloned CLONE_THREAD has an independent user stack, so a naked
 * syscall trampoline JUMPS to the C worker rather than returning through
 * the parent's nonexistent stack frame (same contract as M44/M45). */
__attribute__((naked)) static long launch_worker(void *sp __attribute__((unused)),
                            volatile u32 *ptid __attribute__((unused)),
                            volatile u32 *ctid __attribute__((unused))) {
#if defined(__x86_64__)
    __asm__ volatile(
        "mov %rdx,%r10\nmov %rsi,%rdx\nmov %rdi,%rsi\n"
        "mov $0x7d0f00,%edi\nxor %r8d,%r8d\nmov $56,%eax\n"
        "syscall\ntest %rax,%rax\njz 1f\nret\n"
        "1: sub $8,%rsp\njmp worker_entry\n");
#elif defined(__riscv)
    __asm__ volatile(
        "mv a4,a2\nmv a2,a1\nmv a1,a0\nli a3,0\n"
        "li a0,0x7d0f00\nli a7,220\necall\n"
        "beqz a0,1f\nret\n1: tail worker_entry\n");
#else
    __asm__ volatile(
        "mov x4,x2\nmov x2,x1\nmov x1,x0\nmov x3,xzr\n"
        "movz x0,#0x0f00\nmovk x0,#0x7d,lsl #16\n"
        "mov x8,#220\nsvc #0\ncbz x0,1f\nret\n1: b worker_entry\n");
#endif
}
static long spawn_worker(unsigned role) {
    worker_ready=worker_error=worker_release=tid_parent=0;
    tid_clear=CLEAR_MARKER;
    worker_role=role;
    long sp=(long)(worker_stack+sizeof(worker_stack));
    long tid=launch_worker((void *)sp,&tid_parent,&tid_clear);
    if(tid<=0 || (u32)tid!=tid_parent)fail(40);
    wait_flag(&worker_ready);
    if(worker_error)fail(worker_error);
    return tid;
}
static __attribute__((used,noreturn)) void stress_main(void) {
    test_process_child_tid((long)&fork_tid,1);
    test_process_child_tid(1,0);
    SAY("M103_FORK_CHILD_TID_PASS\n");

    /* First worker has a genuine CLEAR-only pointer, which is still the
     * sentinel during first dispatch and becomes zero at thread exit. */
    long tid=spawn_worker(1);
    if(tid_clear) {
        long ret=SYS3(N_FUTEX,(long)&tid_clear,128,CLEAR_MARKER);
        if(ret!=0 && ret!=-11)fail(41);
    }
    if(tid_clear!=0)fail(42);
    wait_thread_gone(tid);
    SAY("M103_THREAD_CLEAR_ONLY_PASS\n");

    /* Invalid set_tid_address affects only the exiting worker; run
     * repeatedly to expose crashed kernels or unreclaimed Task slots. */
    for(unsigned round=0;round<56;round++) {
        tid=spawn_worker(2);
        wait_thread_gone(tid); /* never reuse stack before confirmed exit */
        if(tid_clear!=CLEAR_MARKER || worker_error)fail(43);
    }
    SAY("M103_INVALID_CLEAR_TID_PASS\n");

    tid=spawn_worker(3);
    worker_release=1;
    (void)SYS3(N_FUTEX,(long)&worker_release,129,1);
    struct ts64 timeout={3,0};
    /* Robust exit uses Linux's shared futex wake key, even for a private
     * process-local robust list: match its key with FUTEX_WAIT (no 128). */
    long w=sys6(N_FUTEX,(long)&pending_node.futex,0,0,
                (long)&timeout,0,0);
    if(w!=0 || worker_error || pending_node.futex!=0)fail(44);
    wait_thread_gone(tid);
    SAY("M103_ROBUST_PENDING_WAKE_PASS\n");
    SAY("M103_NATIVE_ABI_PASS\n");
    fail(0);
}
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall stress_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void) {stress_main();}
#endif
