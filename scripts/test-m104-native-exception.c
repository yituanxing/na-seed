/* M104: freestanding native fault isolation test on x86-64/RV64/AArch64.
 * No libc, emulator-only hooks, signal faking, or injected kernel test code.
 * Exactly this x86 ELF also runs against native Linux as an oracle. */
typedef unsigned long u64;
#if defined(__x86_64__)
#define N_CLONE 56
#define N_EXIT_GROUP 231
#define N_WAIT4 61
#define N_WRITE 1
#elif defined(__riscv) || defined(__aarch64__)
#define N_CLONE 220
#define N_EXIT_GROUP 94
#define N_WAIT4 260
#define N_WRITE 64
#else
#error "M104 requires x86-64, RISC-V 64 or AArch64"
#endif

static inline long sys6(long n, long a, long b, long c, long d,
                        long e, long f) {
#if defined(__x86_64__)
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b),
                     "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
#elif defined(__riscv)
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    register long a7 __asm__("a7") = n;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3),
                     "r"(a4), "r"(a5), "r"(a7) : "memory");
    return a0;
#else
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3),
                     "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
#endif
}
#define SAY(s) ((void)sys6(N_WRITE, 1, (long)(s), sizeof(s)-1, 0, 0, 0))

__attribute__((noreturn)) static void leave(int status) {
    (void)sys6(N_EXIT_GROUP, status, 0, 0, 0, 0, 0);
    for (;;) { }
}
/* Linux SIGILL=4, SIGTRAP=5, SIGFPE=8, SIGSEGV=11. */
enum { ILL=4, TRAP=5, FPE=8, SEGV=11 };
enum { NORMAL=0, INVALID_INSTRUCTION=1, BREAKPOINT=2,
       MEMORY_FAULT=3, INTEGER_DIVIDE_ZERO=4 };

__attribute__((noinline, noreturn)) static void provoke(unsigned reason) {
    switch (reason) {
    case INVALID_INSTRUCTION:
#if defined(__x86_64__)
        __asm__ volatile("ud2" ::: "memory");
#elif defined(__riscv)
        __asm__ volatile(".word 0" ::: "memory");
#else
        __asm__ volatile(".inst 0" ::: "memory"); /* AArch64 UDF #0 */
#endif
        break;
    case BREAKPOINT:
#if defined(__x86_64__)
        __asm__ volatile("int3" ::: "memory");
#elif defined(__riscv)
        __asm__ volatile("ebreak" ::: "memory");
#else
        __asm__ volatile("brk #0" ::: "memory");
#endif
        break;
    case MEMORY_FAULT: {
        /* 0x1000 is below the ELF/user mapping; a genuine user load. */
        volatile u64 *address = (volatile u64 *)0x1000UL;
        volatile u64 value = *address;
        (void)value;
        break;
    }
#if defined(__x86_64__)
    case INTEGER_DIVIDE_ZERO:
        __asm__ volatile("xor %%rdx,%%rdx\n\t"
                         "mov $1,%%rax\n\t"
                         "xor %%rcx,%%rcx\n\t"
                         "divq %%rcx" ::: "rax", "rcx", "rdx", "memory");
        break;
#endif
    default:
        break;
    }
    /* If an exception was ignored/resumed, make that a failing child exit.
     * A signal must be observed via wait4, never emulated via exit(signo). */
    leave(99);
}

static void one(unsigned kind, int expected_signal) {
    /* All three kernels support clone(SIGCHLD, stack=0) as fork-style copy.
     * The parent never shares the child's mutable user memory. */
    long pid = sys6(N_CLONE, 17, 0, 0, 0, 0, 0);
    if (pid < 0) { SAY("M104_FAIL_FORK\n"); leave(10); }
    if (pid == 0) {
        if (kind == NORMAL) leave(7);
        provoke(kind);
    }
    int status = -1;
    long waited = sys6(N_WAIT4, pid, (long)&status, 0, 0, 0, 0);
    if (waited != pid) { SAY("M104_FAIL_WAIT4\n"); leave(11); }
    if (expected_signal) {
        /* wait4 signal termination occupies bits 0..6. Ensure no core
         * dump marker (bit 7), normal exit status, or stopped-child marker
         * is mistaken for successful delivery. The host oracle sets its
         * core-size limit to zero before starting the exact guest ELF. */
        if ((status & 0xff) != expected_signal || (status & ~0xff) != 0) {
            SAY("M104_FAIL_SIGNAL_STATUS\n"); leave(12);
        }
    } else if (status != (7 << 8)) {
        SAY("M104_FAIL_NORMAL_STATUS\n"); leave(13);
    }
}

__attribute__((used, noreturn)) static void stress_main(void) {
    /* The tests must establish that the parent survives multiple faults,
     * then a healthy newly-forked child can still exit and be reaped. */
    one(INVALID_INSTRUCTION, ILL);
    SAY("M104_SIGILL_PASS\n");
    one(BREAKPOINT, TRAP);
    SAY("M104_SIGTRAP_PASS\n");
    one(MEMORY_FAULT, SEGV);
    SAY("M104_SIGSEGV_PASS\n");
#if defined(__x86_64__)
    one(INTEGER_DIVIDE_ZERO, FPE);
    SAY("M104_SIGFPE_PASS\n");
#endif
    one(NORMAL, 0);
    SAY("M104_POST_FAULT_FORK_PASS\n");
    SAY("M104_NATIVE_EXCEPTION_PASS\n");
    leave(0);
}
#if defined(__x86_64__)
/* Linux/NaSeed ELF entry gives a 16-byte aligned RSP, not an ordinary
 * SysV function-call frame. Establish the call ABI before entering C. */
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall stress_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void) { stress_main(); }
#endif
