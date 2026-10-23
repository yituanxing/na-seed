/* A real native zlib/DEFLATE application linked from ten+ independently
 * compiled files. zlib 1.3.1 is fetched byte-for-byte from upstream.
 * Z_SOLO (official freestanding build flag) lets the test supply allocator
 * callbacks instead of requiring a guest malloc/stdio sysroot.
 * Real compress+decompress correctness controls the exit status. */
#include "zlib.h"
static unsigned char heap[1024*1024];
static unsigned long used;
static voidpf arena_alloc(voidpf ignored,uInt items,uInt size) {
    unsigned long n=(unsigned long)items*(unsigned long)size;
    unsigned long start=(used+15UL)&~15UL;
    (void)ignored;
    if (items && n/items!=size) return Z_NULL;
    if (start>sizeof(heap) || n>sizeof(heap)-start) return Z_NULL;
    used=start+n;
    return (voidpf)(heap+start);
}
static void arena_free(voidpf ignored,voidpf ptr) {(void)ignored;(void)ptr;}
static unsigned char original[16384];
static unsigned char encoded[20000];
static unsigned char decoded[16384];
static int check_zlib(void) {
    z_stream s={0};
    for(unsigned i=0;i<sizeof(original);i++)
        original[i]=(unsigned char)((i*37U+(i>>5))&255U);
    s.zalloc=arena_alloc; s.zfree=arena_free;
    if (deflateInit(&s,Z_BEST_COMPRESSION)!=Z_OK) return 11;
    s.next_in=original; s.avail_in=sizeof(original);
    s.next_out=encoded; s.avail_out=sizeof(encoded);
    int err=deflate(&s,Z_FINISH);
    if (err!=Z_STREAM_END || s.avail_in) return 12;
    unsigned compressed=s.total_out;
    if (deflateEnd(&s)!=Z_OK) return 13;
    if (!compressed || compressed>=sizeof(original)) return 14;
    z_stream d={0};
    d.zalloc=arena_alloc; d.zfree=arena_free;
    if (inflateInit(&d)!=Z_OK) return 15;
    d.next_in=encoded; d.avail_in=compressed;
    d.next_out=decoded; d.avail_out=sizeof(decoded);
    err=inflate(&d,Z_FINISH);
    if(err!=Z_STREAM_END || d.total_out!=sizeof(original)) return 16;
    if(inflateEnd(&d)!=Z_OK) return 17;
    for(unsigned i=0;i<sizeof(original);i++)
        if(decoded[i]!=original[i]) return 18;
    return 0;
}
#if defined(__x86_64__)
static long sys3(long nr,long a,long b,long c) {
    long out; __asm__ volatile("syscall":"=a"(out)
        :"a"(nr),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");
    return out;
}
#define SYS_WRITE 1
#define SYS_EXIT 60
#elif defined(__riscv) && __riscv_xlen==64
static long sys3(long nr,long a,long b,long c) {
    register long a0 __asm__("a0")=a;
    register long a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c;
    register long a7 __asm__("a7")=nr;
    __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a7):"memory");
    return a0;
}
#define SYS_WRITE 64
#define SYS_EXIT 93
#elif !defined(__aarch64__)
#error "M36 requires x86-64, RISC-V64 or AArch64"
#endif
void _start(void) {
    static const char msg[]="naseed-upstream-zlib-pass\n";
    int err=check_zlib();
#if defined(__aarch64__)
    if(err) {
        __asm__ volatile(
            "mov x0, %0\nmov x8, #93\n.long 0xd4000001\n"
            : : "r"((long)err) : "memory");
    } else {
        /* Original TinyCC ARM64 asm does not implement 'svc' mnemonic
         * or correct independent x0/x8 extended-asm operand ties.
         * The .long is the real SVC #0 instruction, not a stub. */
        __asm__ volatile(
            "mov x1, %0\nmov x0, #1\nmov x2, #26\n"
            "mov x8, #64\n.long 0xd4000001\n"
            "mov x0, #0\nmov x8, #93\n.long 0xd4000001\n"
            : : "r"((long)msg) : "memory");
    }
#else
    if(!err && sys3(SYS_WRITE,1,(long)msg,sizeof(msg)-1)!=sizeof(msg)-1)
        err=19;
    sys3(SYS_EXIT,err,0,0);
#endif
    for(;;) {}
}
