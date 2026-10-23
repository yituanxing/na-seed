/* Genuine tiny Linux executable produced by upstream TinyCC inside NaSeed.
 * Freestanding: write/exit syscalls are real, not simulated guest output. */
static long linux_syscall3(long nr,long a,long b,long c) {
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}
void _start(void) {
    static const char message[]="naseed-native-tcc-elf-ran\n";
    linux_syscall3(1,1,(long)message,sizeof(message)-1);
    linux_syscall3(60,0,0,0);
    for(;;) {}
}
