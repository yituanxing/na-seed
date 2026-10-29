/* M105 native Linux/NaSeed x86-64, RV64, ARM64 program. No libc.
 * Actual CPU executes JIT code written via mmap RW -> mprotect RX.
 * Fork child privately rewrites instruction, exercises real COW + I-cache.
 * Parent must still execute its original instructions after wait4. */
typedef unsigned long u64;
#if defined(__x86_64__)
#define N_MMAP 9
#define N_MPROTECT 10
#define N_MUNMAP 11
#define N_CLONE 56
#define N_WAIT4 61
#define N_WRITE 1
#define N_EXIT_GROUP 231
#elif defined(__riscv) || defined(__aarch64__)
#define N_MMAP 222
#define N_MPROTECT 226
#define N_MUNMAP 215
#define N_CLONE 220
#define N_WAIT4 260
#define N_WRITE 64
#define N_EXIT_GROUP 94
#else
#error "M105 native JIT requires one of x86-64/RV64/AArch64"
#endif
static inline long call6(long n,long a,long b,long c,long d,long e,long f) {
#if defined(__x86_64__)
    register long r10 __asm__("r10")=d;
    register long r8 __asm__("r8")=e;
    register long r9 __asm__("r9")=f;
    long ret;
    __asm__ volatile("syscall":"=a"(ret):"a"(n),"D"(a),"S"(b),
                     "d"(c),"r"(r10),"r"(r8),"r"(r9):
                     "rcx","r11","memory");
    return ret;
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
#define WRITE(msg) ((void)call6(N_WRITE,1,(long)(msg),sizeof(msg)-1,0,0,0))
__attribute__((noreturn)) static void leave(int n) {
    (void)call6(N_EXIT_GROUP,n,0,0,0,0,0);
    for(;;) { }
}
static void write_code(volatile unsigned char *p,unsigned answer) {
#if defined(__x86_64__)
    /* mov eax, imm32 ; ret */
    p[0]=0xb8;p[1]=(unsigned char)answer;
    p[2]=0;p[3]=0;p[4]=0;p[5]=0xc3;
#elif defined(__riscv)
    /* addi a0,x0,imm12 ; jalr x0,0(ra). Instructions are 32-bit little. */
    u64 op=((u64)answer<<20)|0x513UL;
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(op>>(i*8));
    op=0x00008067UL;
    for(unsigned i=0;i<4;i++)p[4+i]=(unsigned char)(op>>(i*8));
#else
    /* mov w0,#imm16 ; ret, little endian. */
    u64 op=0x52800000UL|((u64)answer<<5);
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(op>>(i*8));
    op=0xd65f03c0UL;
    for(unsigned i=0;i<4;i++)p[4+i]=(unsigned char)(op>>(i*8));
#endif
}
__attribute__((noinline)) static int run_code(const volatile unsigned char *p) {
    int (*function)(void)=(int (*)(void))(u64)p;
    __asm__ volatile("":::"memory");
    return function();
}
__attribute__((used,noreturn)) static void test_main(void) {
    long map=call6(N_MMAP,0,4096,3,0x22,-1,0);
    if(map<0) {WRITE("M105_JIT_FAIL_MMAP\n");leave(20);}
    volatile unsigned char *code=(volatile unsigned char *)(u64)map;
    write_code(code,42);
    if(call6(N_MPROTECT,map,4096,5,0,0,0)) {
        WRITE("M105_JIT_FAIL_MPROTECT_RX\n");leave(21);
    }
    if(run_code(code)!=42) {
        WRITE("M105_JIT_FAIL_INITIAL_EXEC\n");leave(22);
    }
    WRITE("M105_JIT_MPROTECT_EXEC_PASS\n");
    long pid=call6(N_CLONE,17,0,0,0,0,0);
    if(pid<0) {WRITE("M105_JIT_FAIL_CLONE\n");leave(23);}
    if(!pid) {
        if(call6(N_MPROTECT,map,4096,3,0,0,0))leave(30);
        write_code(code,43);
        if(call6(N_MPROTECT,map,4096,5,0,0,0))leave(31);
        if(run_code(code)!=43)leave(32);
        leave(7);
    }
    int status=-1;
    long waited=call6(N_WAIT4,pid,(long)&status,0,0,0,0);
    if(waited!=pid || status!=(7<<8)) {
        WRITE("M105_JIT_FAIL_CHILD_STATUS\n");leave(24);
    }
    if(run_code(code)!=42) {
        WRITE("M105_JIT_FAIL_PARENT_COW\n");leave(25);
    }
    if(call6(N_MUNMAP,map,4096,0,0,0,0)) {
        WRITE("M105_JIT_FAIL_MUNMAP\n");leave(26);
    }
    WRITE("M105_NATIVE_JIT_COW_PASS\n");
    leave(0);
}
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall test_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void) {test_main();}
#endif
