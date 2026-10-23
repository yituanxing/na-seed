/* Replacement image must execute only after former group killed.
 * Confirm gettid == getpid (worker was promoted to leader), and robust
 * registration was reset. Executed via native Linux execve. */
typedef unsigned long u64;
#if defined(__x86_64__)
#define NR_getpid 39
#define NR_gettid 186
#define NR_getrobust 274
#define NR_write 1
#define NR_exit_group 231
#else
#define NR_getpid 172
#define NR_gettid 178
#define NR_getrobust 100
#define NR_write 64
#define NR_exit_group 94
#endif
static inline long sys3(long n,long a,long b,long c){
#if defined(__x86_64__)
    long r;
    __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):
           "rcx","r11","memory");
    return r;
#elif defined(__riscv)
    register long a0 __asm__("a0")=a,a1 __asm__("a1")=b,a2 __asm__("a2")=c;
    register long a7 __asm__("a7")=n;
    __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a7):"memory");
    return a0;
#else
    register long x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0":"+r"(x0):"r"(x1),"r"(x2),"r"(x8):"memory");
    return x0;
#endif
}
static __attribute__((used,noreturn)) void target_main(void){
    long tid=sys3(NR_gettid,0,0,0),pid=sys3(NR_getpid,0,0,0);
    u64 head=1,length=0;
    long r=sys3(NR_getrobust,0,(long)&head,(long)&length);
    if(tid<=0 || tid!=pid || r!=0 || head!=0 || length!=24)
        sys3(NR_exit_group,67,0,0);
    static const char msg[]="M46_EXEC_TARGET_PASS\n";
    (void)sys3(NR_write,1,(long)msg,sizeof(msg)-1);
    sys3(NR_exit_group,0,0,0);
    for(;;){}
}
#if defined(__x86_64__)
__attribute__((naked,noreturn)) void _start(void) {
    __asm__ volatile("and $-16,%rsp\ncall target_main\nud2\n");
}
#else
__attribute__((noreturn)) void _start(void){target_main();}
#endif
