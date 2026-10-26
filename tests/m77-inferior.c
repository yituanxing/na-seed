/* M77 original GDB C/DWARF fixture, cross-compiled only on CI host.
 * No guest toolchain or debug stubs. Debugger must alter a live C local.
 * Only Linux write, nanosleep and exit are required, through the native ISA ABI. */
static long linux_call3(long n,long a,long b,long c) {
#if defined(__x86_64__)
    long r;
    __asm__ volatile("syscall" : "=a"(r) :
                     "a"(n),"D"(a),"S"(b),"d"(c) : "rcx","r11","memory");
    return r;
#elif defined(__riscv)
    register long a0 __asm__("a0")=a;
    register long a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c;
    register long a7 __asm__("a7")=n;
    __asm__ volatile("ecall" : "+r"(a0) :
                     "r"(a1),"r"(a2),"r"(a7) : "memory");
    return a0;
#elif defined(__aarch64__)
    register long x0 __asm__("x0")=a;
    register long x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0" : "+r"(x0) :
                     "r"(x1),"r"(x2),"r"(x8) : "memory");
    return x0;
#else
#error unrecognized target
#endif
}
volatile int m77_observed;
__attribute__((noinline,used)) int m77_compute(int bonus) {
    volatile int local = 17;
    local += bonus; /* M77_LIVE_ATTACH_BREAK */
    m77_observed = local;
    return local;
}
/* Repeatedly enter this function after real blocking nanosleep syscalls.
 * An external GDB process MUST attach by PID, plant a breakpoint in this
 * existing live task, modify the DWARF local, detach and let it finish. */
__attribute__((noreturn,used)) void _start(void) {
    static const char ready[]="M77_INFERIOR_READY\\n";
#if defined(__x86_64__)
    (void)linux_call3(1,1,(long)ready,sizeof(ready)-1);
    const long sleep_nr=35, exit_nr=60;
#else
    (void)linux_call3(64,1,(long)ready,sizeof(ready)-1);
    const long sleep_nr=101, exit_nr=93;
#endif
    struct {long sec,nsec;} delay={0,100000000};
    for(unsigned tries=0;tries<500;tries++) {
        (void)linux_call3(sleep_nr,(long)&delay,0,0);
        int result=m77_compute(5);
        if(result==99 && m77_observed==99) {
            static const char ok[]="M77_ATTACH_MUTATION_PASS\\n";
#if defined(__x86_64__)
            (void)linux_call3(1,1,(long)ok,sizeof(ok)-1);
#else
            (void)linux_call3(64,1,(long)ok,sizeof(ok)-1);
#endif
            (void)linux_call3(exit_nr,0,0,0);
            for(;;){}
        }
    }
    static const char bad[]="M77_ATTACH_MUTATION_TIMEOUT_FAIL\\n";
#if defined(__x86_64__)
    (void)linux_call3(1,1,(long)bad,sizeof(bad)-1);
#else
    (void)linux_call3(64,1,(long)bad,sizeof(bad)-1);
#endif
    (void)linux_call3(exit_nr,23,0,0);
    for(;;){}
}
