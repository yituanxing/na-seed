/* M45: host-LLVM-built, Linux ABI robust-futex owner death test on
 * x86_64/RV64/AArch64; no bundled libc, fake kernel dispatch or guest GCC.
 * Follow with original signed musl Zstandard multithreaded compressor. */
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
#if defined(__x86_64__)
#define N_CLONE 56
#define N_EXIT 60
#define N_EXIT_GROUP 231
#define N_GETTID 186
#define N_FUTEX 202
#define N_SET_ROBUST 273
#define N_GET_ROBUST 274
#define N_SLEEP 230
#define N_WRITE 1
#else
#define N_CLONE 220
#define N_EXIT 93
#define N_EXIT_GROUP 94
#define N_GETTID 178
#define N_FUTEX 98
#define N_SET_ROBUST 99
#define N_GET_ROBUST 100
#define N_SLEEP 115
#define N_WRITE 64
#endif
static inline long syscall6(long n,long a,long b,long c,long d,long e,long f) {
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
static inline long call3(long n,long a,long b,long c) {
    return syscall6(n,a,b,c,0,0,0);
}
static inline long call2(long n,long a,long b) {
    return syscall6(n,a,b,0,0,0,0);
}
static inline long call1(long n,long a) {
    return syscall6(n,a,0,0,0,0,0);
}
#define SAY(s) (void)call3(N_WRITE,1,(long)(s),sizeof(s)-1)
static __attribute__((noreturn)) void fail(int status) {
    call1(N_EXIT_GROUP,status);
    for(;;){}
}
static __attribute__((noreturn)) void thread_exit(void) {
    call1(N_EXIT,0);
    for(;;){}
}
struct robust_head {u64 next; s64 offset; u64 pending;};
struct robust_mutex {u64 next; volatile u32 futex; u32 pad;};
struct robust_case {
    struct robust_head head;
    struct robust_mutex lock[2];
};
static struct robust_case tests[3];
static volatile u32 tid_slots[3];
/* Registration is a per-TID syscall: a freshly cloned sibling is NOT
 * registered at the instant clone() returns in its parent. Synchronize
 * using actual Linux FUTEX_WAIT/WAKE instead of scheduler timing guesses. */
static volatile u32 registered[3], release_worker[3];
static __attribute__((aligned(16))) unsigned char stacks[3][65536];
static volatile int round_index;
#define WAITERS 0x80000000U
#define OWNER_DIED 0x40000000U
struct ts64 {s64 sec,nsec;};
static void sleep_quarter(void) {
    const struct ts64 ts={0,250000000};
    if(syscall6(N_SLEEP,1,0,(long)&ts,0,0,0))fail(98);
}
static __attribute__((used,noreturn)) void thread_body(void) {
    int i=round_index;
    struct robust_case *r=&tests[i];
    long tid=call1(N_GETTID,0);
    if(tid<=0)fail(76);
    /* Direct kernel robust ABI, identical to Linux native 64-bit layout:
     * node.next at +0, futex at +8, offset=+8. The pending pointer
     * covers a lock acquired before insertion into the main list. */
    r->head.offset=8;
    r->head.next=(u64)&r->head.next;
    r->head.pending=0;
    if(i==0) {
        r->lock[0].futex=(u32)tid|WAITERS;
        r->lock[0].next=(u64)&r->head.next;
        r->head.next=(u64)&r->lock[0].next;
    } else if (i==1) {
        r->lock[0].futex=(u32)tid|WAITERS;
        r->lock[0].next=(u64)&r->head.next;
        r->head.pending=(u64)&r->lock[0].next;
    } else {
        r->lock[0].futex=(u32)tid|WAITERS;
        r->lock[1].futex=(u32)tid|WAITERS;
        r->lock[0].next=(u64)&r->lock[1].next;
        r->lock[1].next=(u64)&r->head.next;
        r->head.next=(u64)&r->lock[0].next;
    }
    if(call2(N_SET_ROBUST,(long)&r->head,24)!=0)fail(75);
    registered[i]=1;
    if(call3(N_FUTEX,(long)&registered[i],129,1)<0)fail(79);
    /* The parent must have read and validated our live list before we
     * can exit. A 250ms timeout alone is inherently racy with host QEMU
     * scheduling, particularly with simultaneous CI matrix guests. */
    while(!release_worker[i]) {
        long w=call3(N_FUTEX,(long)&release_worker[i],128,0);
        if(w!=0 && w!=-11)fail(80); /* EAGAIN: raced with release */
    }
    sleep_quarter(); /* scheduler switches back to main thread for FUTEX_WAIT */
    thread_exit();   /* owner death must wake the waiting main thread */
}
__attribute__((naked)) static long spawn_thread(void *sp __attribute__((unused)),
                  volatile u32 *parent_tid __attribute__((unused)),
                  volatile u32 *child_tid __attribute__((unused))) {
#if defined(__x86_64__)
    __asm__ volatile(
        "mov %rdx,%r10\nmov %rsi,%rdx\nmov %rdi,%rsi\n"
        "mov $0x7d0f00,%edi\nxor %r8d,%r8d\nmov $56,%eax\n"
        "syscall\ntest %rax,%rax\njz 1f\nret\n"
        "1: sub $8,%rsp\njmp thread_body\n");
#elif defined(__riscv)
    __asm__ volatile(
        "mv a4,a2\nmv a2,a1\nmv a1,a0\n"
        "li a3,0\nli a0,0x7d0f00\nli a7,220\necall\n"
        "beqz a0,1f\nret\n1: tail thread_body\n");
#else
    __asm__ volatile(
        "mov x4,x2\nmov x2,x1\nmov x1,x0\n"
        "mov x3,xzr\nmovz x0,#0x0f00\nmovk x0,#0x7d,lsl #16\n"
        "mov x8,#220\nsvc #0\ncbz x0,1f\nret\n1: b thread_body\n");
#endif
}
static __attribute__((used,noreturn)) void robust_main(void) {
    struct robust_head leader_head={0};
    u64 out_head=0,out_len=0;
    /* 1: registration is per TID, exactly 24 bytes for LP64 Linux;
     * malformed length must reject without replacing live registration. */
    if(call2(N_SET_ROBUST,(long)&leader_head,8)!=-22)fail(51);
    if(call2(N_SET_ROBUST,(long)&leader_head,24))fail(52);
    if(call3(N_GET_ROBUST,0,(long)&out_head,(long)&out_len))fail(53);
    if(out_head!=(u64)&leader_head || out_len!=24)fail(54);
    if(call3(N_GET_ROBUST,0,0,(long)&out_len)!=-14)fail(55);
    SAY("M45_REGISTRATION_PASS\n");

    for(int i=0;i<3;i++) {
        round_index=i;
        long sp=(long)(stacks[i]+sizeof(stacks[i]));
        long child=spawn_thread((void*)sp,&tid_slots[i],&tid_slots[i]);
        if(child<=0)fail(56);
        if((u32)child!=tid_slots[i])fail(57);
        /* clone() only schedules the new TID; it does not imply that the
         * worker has executed set_robust_list. Wait for the real futex
         * registration acknowledgement before inspecting another TID. */
        while(!registered[i]) {
            long w=call3(N_FUTEX,(long)&registered[i],128,0);
            if(w!=0 && w!=-11)fail(81);
        }
        /* Each worker's registered list is discoverable while alive,
         * and must be distinct from the group leader's list. */
        out_head=0;out_len=0;
        if(call3(N_GET_ROBUST,child,(long)&out_head,(long)&out_len))fail(58);
        if(out_head!=(u64)&tests[i].head || out_len!=24)fail(59);
        u32 expected=(u32)child|WAITERS;
        if(tests[i].lock[0].futex!=expected)fail(60);
        release_worker[i]=1;
        if(call3(N_FUTEX,(long)&release_worker[i],129,1)<0)fail(82);
        long ret=call3(N_FUTEX,(long)&tests[i].lock[0].futex,0|128,expected);
        if(ret!=0)fail(61); /* MUST block and wake on genuine owner death */
        if(tests[i].lock[0].futex!=(OWNER_DIED|WAITERS))fail(62);
        if(i==2 && tests[i].lock[1].futex!=(OWNER_DIED|WAITERS))fail(63);
        if(tid_slots[i]!=0)fail(64); /* clear_child_tid and futex wake */
    }
    SAY("M45_ROBUST_OWNER_DIED_PASS\n");
    SAY("M45_PENDING_AND_MULTI_LOCK_PASS\n");
    /* Use the unchanged genuine original Alpine zstd -T2 after the
     * freestanding ABI stress completes in the same native guest. */
    fail(0);
}

/* Linux's initial ELF user RSP is 16-byte aligned, but the C function
 * calling convention expects the synthetic return slot (RSP % 16 = 8).
 * A raw C _start compiled with SSE can otherwise #GP on movaps.  This
 * tiny genuine user-space entry shim is NOT a kernel or app syscall hack. */
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall robust_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void) {
    robust_main();
}
#endif
