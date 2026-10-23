/* Original TinyCC generates and links this *inside NaSeed*. No guest
 * proxy, C runtime, or libc startup: Linux write/exit syscalls are real.
 * The same C source must build and execute on all three native ISAs. */
#if defined(__x86_64__)
static long call3(long nr,long a,long b,long c) {
    long v;
    __asm__ volatile("syscall" : "=a"(v)
                     : "a"(nr),"D"(a),"S"(b),"d"(c)
                     : "rcx","r11","memory");
    return v;
}
enum { SYS_WRITE=1, SYS_EXIT=60 };
#elif defined(__aarch64__)
static long call3(long nr,long a,long b,long c) {
    register long x0 __asm__("x0")=a;
    register long x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c;
    register long x8 __asm__("x8")=nr;
    /* TinyCC upstream ARM64 inline assembler lacks the svc mnemonic,
     * but supports the standard .long directive. 0xd4000001 is the
     * architecturally specified encoding of SVC #0. Real kernel entry,
     * registers and returned errno are still exercised unchanged. */
    __asm__ volatile(".long 0xd4000001" : "+r"(x0)
                     : "r"(x1),"r"(x2),"r"(x8) : "memory");
    return x0;
}
enum { SYS_WRITE=64, SYS_EXIT=93 };
#elif defined(__riscv) && __riscv_xlen==64
static long call3(long nr,long a,long b,long c) {
    register long a0 __asm__("a0")=a;
    register long a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c;
    register long a7 __asm__("a7")=nr;
    __asm__ volatile("ecall" : "+r"(a0)
                     : "r"(a1),"r"(a2),"r"(a7) : "memory");
    return a0;
}
enum { SYS_WRITE=64, SYS_EXIT=93 };
#else
#error "M34 requires native x86-64, RISC-V64 or AArch64"
#endif
void _start(void) {
    static const char hello[]="naseed-three-isa-build-exec\n";
    long n=call3(SYS_WRITE,1,(long)hello,sizeof(hello)-1);
    call3(SYS_EXIT,n==sizeof(hello)-1?0:13,0,0);
    for(;;) {}
}
