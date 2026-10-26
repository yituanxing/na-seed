/* M76 original GDB C/DWARF fixture, cross-compiled only on CI host.
 * No guest toolchain or debug stubs. Debugger must alter a live C local.
 * Only Linux write and exit are required, through the native ISA ABI. */
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
volatile int m76_observed;
__attribute__((noinline,used)) int m76_compute(int bonus) {
    volatile int local = 17;
    local += bonus; /* M76_SOURCE_BREAK */
    m76_observed = local;
    return local;
}
__attribute__((noreturn,used)) void _start(void) {
    int result=m76_compute(5);
    if(result==99 && m76_observed==99) {
        static const char ok[]="M76_C_VARIABLE_CHANGED_PASS\n";
#if defined(__x86_64__)
        (void)linux_call3(1,1,(long)ok,sizeof(ok)-1);
#else
        (void)linux_call3(64,1,(long)ok,sizeof(ok)-1);
#endif
    } else {
        static const char bad[]="M76_C_VARIABLE_UNCHANGED_FAIL\n";
#if defined(__x86_64__)
        (void)linux_call3(1,1,(long)bad,sizeof(bad)-1);
#else
        (void)linux_call3(64,1,(long)bad,sizeof(bad)-1);
#endif
    }
#if defined(__x86_64__)
    (void)linux_call3(60,result==99 && m76_observed==99?0:23,0,0);
#else
    (void)linux_call3(93,result==99 && m76_observed==99?0:23,0,0);
#endif
    for(;;){}
}
