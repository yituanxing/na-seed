/* Real NIST SP 800-38A vectors on the UNMODIFIED tiny-AES-c sources.
 * Three separate native guest .o files: upstream aes.c, freestanding
 * libc subset, and this test harness; GNU Make invokes all builds.
 * A nonzero exit proves a real cryptographic mismatch, not a marker. */
#include "aes.h"
static int eq(const uint8_t *a, const uint8_t *b, unsigned n) {
    for (unsigned i=0;i<n;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
static int aes_check(void) {
    static const uint8_t key[16]={
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
        0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
    static const uint8_t plain[32]={
        0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
        0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
        0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,
        0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51};
    static const uint8_t ecb[16]={
        0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
        0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
    static const uint8_t cbc[32]={
        0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,
        0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
        0x50,0x86,0xcb,0x9b,0x50,0x72,0x19,0xee,
        0x95,0xdb,0x11,0x3a,0x91,0x76,0x78,0xb2};
    static const uint8_t ctr_iv[16]={
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,
        0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
    static const uint8_t ctr[16]={
        0x87,0x4d,0x61,0x91,0xb6,0x20,0xe3,0x26,
        0x1b,0xef,0x68,0x64,0x99,0x0d,0xb6,0xce};
    uint8_t block[32],iv[16];
    struct AES_ctx ctx;
    for(unsigned i=0;i<32;i++) block[i]=plain[i];
    AES_init_ctx(&ctx,key);
    AES_ECB_encrypt(&ctx,block);
    if (!eq(block,ecb,16)) return 11;
    AES_ECB_decrypt(&ctx,block);
    if (!eq(block,plain,16)) return 12;
    for(unsigned i=0;i<16;i++) iv[i]=(uint8_t)i;
    AES_init_ctx_iv(&ctx,key,iv);
    AES_CBC_encrypt_buffer(&ctx,block,32);
    if (!eq(block,cbc,32)) return 13;
    AES_init_ctx_iv(&ctx,key,iv);
    AES_CBC_decrypt_buffer(&ctx,block,32);
    if (!eq(block,plain,32)) return 14;
    for(unsigned i=0;i<32;i++) block[i]=plain[i];
    AES_init_ctx_iv(&ctx,key,ctr_iv);
    AES_CTR_xcrypt_buffer(&ctx,block,16);
    if (!eq(block,ctr,16)) return 15;
    AES_init_ctx_iv(&ctx,key,ctr_iv);
    AES_CTR_xcrypt_buffer(&ctx,block,16);
    if (!eq(block,plain,16)) return 16;
    return 0;
}
#if defined(__x86_64__)
static long call3(long nr,long a,long b,long c) {
    long out;__asm__ volatile("syscall":"=a"(out)
      :"a"(nr),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");
    return out;
}
enum { SYS_WRITE=1, SYS_EXIT=60 };
#elif defined(__riscv) && __riscv_xlen==64
static long call3(long nr,long a,long b,long c) {
    register long a0 __asm__("a0")=a;
    register long a1 __asm__("a1")=b;
    register long a2 __asm__("a2")=c;
    register long a7 __asm__("a7")=nr;
    __asm__ volatile("ecall":"+r"(a0):"r"(a1),"r"(a2),"r"(a7):"memory");
    return a0;
}
enum { SYS_WRITE=64, SYS_EXIT=93 };
#elif !defined(__aarch64__)
#error M35 requires Linux LP64 x86, RISCV64 or AArch64
#endif
void _start(void) {
    static const char msg[]="naseed-upstream-aes128-pass\n";
    int err=aes_check();
#if defined(__aarch64__)
    /* Pinned original TinyCC ARM64 extended asm does not parse 'svc'
     * and has a register-tying bug. Use its supported mov and .long
     * instructions to make the genuine Linux SVC #0 (0xd4000001).
     * A failed NIST vector exits nonzero without printing PASS. */
    if (err) {
        __asm__ volatile(
            "mov x0, %0\nmov x8, #93\n.long 0xd4000001\n"
            : : "r"((long)err) : "memory");
    } else {
        __asm__ volatile(
            "mov x1, %0\nmov x0, #1\nmov x2, #28\n"
            "mov x8, #64\n.long 0xd4000001\n"
            "mov x0, #0\nmov x8, #93\n.long 0xd4000001\n"
            : : "r"((long)msg) : "memory");
    }
#else
    if (!err && call3(SYS_WRITE,1,(long)msg,sizeof(msg)-1)
                   !=sizeof(msg)-1) err=19;
    call3(SYS_EXIT,err,0,0);
#endif
    for (;;) {}
}
