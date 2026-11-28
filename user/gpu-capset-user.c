/* SPDX-License-Identifier: MIT
 * A real x86-64 Ring-3 ELF, not a kernel self-test or a synthetic GPU reply.
 * Exercise Linux DRM VirtGPU GETPARAM/GET_CAPS against the exclusively owned
 * original TGOSKits Rust driver and QEMU virglrenderer. This is a capability
 * client, NOT an OpenGL rendering workload or a substitute for Mesa/glmark2.
 */
#include "../kernel/nr.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static inline long call3(long nr,long a,long b,long c) {
    long result;
    __asm__ volatile("syscall" : "=a"(result) :
                     "a"(nr),"D"(a),"S"(b),"d"(c) :
                     "rcx","r11","memory");
    return result;
}
static inline long call4(long nr,long a,long b,long c,long d) {
    long result;
    register long r10 __asm__("r10")=d;
    __asm__ volatile("syscall" : "=a"(result) :
                     "a"(nr),"D"(a),"S"(b),"d"(c),"r"(r10) :
                     "rcx","r11","memory");
    return result;
}
static void out(const char *message,size_t count) {
    (void)call3(__NR_write,1,(long)(uintptr_t)message,(long)count);
}
static __attribute__((noreturn)) void quit(int status) {
    (void)call3(__NR_exit,status,0,0);
    for(;;) { __asm__ volatile("pause"); }
}

struct getparam {uint64_t param,value;};
struct getcaps {
    uint32_t cap_set_id,cap_set_ver;
    uint64_t addr;
    uint32_t size,pad;
};
_Static_assert(sizeof(struct getparam)==16,"Linux GETPARAM ABI");
_Static_assert(sizeof(struct getcaps)==24,"Linux GET_CAPS ABI");

__attribute__((noreturn)) void _start(void) {
    static const char node[]="/dev/dri/renderD128";
    static const char passed[]=
        "[USER GPU CAPSET PASS] original Linux ioctl -> Rust VirtIoGpu -> QEMU VirGL\n";
    static const char failed[]="[USER GPU CAPSET FAIL]\n";
    // No libc or forged output. NaSeed must resolve a real character inode.
    long fd=call4(__NR_openat,-100,(long)(uintptr_t)node,2,0);
    if(fd<0)goto fail;
    struct {uint32_t value,guard;} feature={
        .value=UINT32_MAX,.guard=UINT32_C(0xa5b4c3d2)
    };
    struct getparam p={.param=1,.value=(uint64_t)(uintptr_t)&feature.value};
    // Linux's ioctl returns *int*, not u64; check both ABI and true status.
    if(call3(__NR_ioctl,fd,UINT32_C(0xc0106443),
             (long)(uintptr_t)&p)!=0 || feature.value!=0 ||
       feature.guard!=UINT32_C(0xa5b4c3d2))goto close_fail;
    uint32_t mask=0;
    p.param=7;p.value=(uint64_t)(uintptr_t)&mask;
    if(call3(__NR_ioctl,fd,UINT32_C(0xc0106443),
             (long)(uintptr_t)&p)!=0 || !(mask&(UINT32_C(1)<<1)))
        goto close_fail;
    uint8_t capset[4096];
    volatile uint8_t *initial=capset;
    for(size_t i=0;i<sizeof(capset);i++)initial[i]=0;
    struct getcaps caps={
        .cap_set_id=1,.cap_set_ver=0,.addr=(uint64_t)(uintptr_t)capset,
        .size=(uint32_t)sizeof(capset),.pad=0
    };
    if(call3(__NR_ioctl,fd,UINT32_C(0xc0186449),
             (long)(uintptr_t)&caps)!=0)goto close_fail;
    bool any=false;
    for(size_t i=0;i<64;i++)if(capset[i]){any=true;break;}
    if(!any)goto close_fail;
    (void)call3(__NR_close,fd,0,0);
    out(passed,sizeof(passed)-1);
    quit(0);
close_fail:
    (void)call3(__NR_close,fd,0,0);
fail:
    out(failed,sizeof(failed)-1);
    quit(1);
}
