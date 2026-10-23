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
/* The pinned upstream TinyCC ARM backend has a real extended-inline-asm
 * register-allocation defect: x0 is overwritten with the syscall number
 * while x8 is correctly loaded. AArch64's syscall ABI is x8=nr,x0=arg0.
 * Keep the original compiler and bypass only its faulty operand tying by
 * placing all four ABI register moves inside one asm instruction sequence.
 * TinyCC supports the standard A64 mov + .long directives, but not the
 * svc mnemonic. The encoded 0xd4000001 IS the real architectural SVC #0.
 * Linux/QEMU-user AND native NaSeed both verify actual write+exit below. */
#define NAS_AARCH64_RAW_SYSCALL 1
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
#if defined(NAS_AARCH64_RAW_SYSCALL)
    __asm__ volatile(
        "mov x1, %0\n"
        "mov x0, #1\n"
        "mov x2, #28\n"
        "mov x8, #64\n"
        ".long 0xd4000001\n"
        "mov x0, #0\n"
        "mov x8, #93\n"
        ".long 0xd4000001\n"
        : : "r"((long)hello) : "memory");
#else
    long n=call3(SYS_WRITE,1,(long)hello,sizeof(hello)-1);
    call3(SYS_EXIT,n==sizeof(hello)-1?0:13,0,0);
#endif
    for(;;) {}
}
