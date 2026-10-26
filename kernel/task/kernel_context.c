#include "kernel_context.h"
#include <stdint.h>
#include <stdbool.h>
/* Set up the exact callee-saved frame that the ISA-native swap restores.
 * A completed CPU trap/user stack cannot be substituted for a kernel frame. */
bool nas_kernel_context_prepare(struct nas_kernel_context *ctx,void *stack,
                                uint64_t bytes,void (*entry)(void)) {
    if(!ctx || !stack || !entry || bytes<512)return false;
    uintptr_t bottom=(uintptr_t)stack;
    if((bottom&15U) || bytes>UINTPTR_MAX-bottom)return false;
    uintptr_t top=(bottom+bytes)&~(uintptr_t)15U;
#if defined(__x86_64__)
    /* pop r15,r14,r13,r12,rbp,rbx; ret. SysV first-entry rsp%16=8. */
    const unsigned words=8U;
    if(top-bottom<words*8U+16U)return false;
    uint64_t *saved=(uint64_t *)(top-words*8U);
    for(unsigned i=0;i<words;i++)saved[i]=0;
    saved[6]=(uint64_t)(uintptr_t)entry;
#elif defined(__riscv) && __riscv_xlen==64
    /* ra + s0..s11. Restored sp is 16-byte aligned. */
    const unsigned words=13U;
    if(top-bottom<words*8U+16U)return false;
    uint64_t *saved=(uint64_t *)(top-words*8U);
    for(unsigned i=0;i<words;i++)saved[i]=0;
    saved[0]=(uint64_t)(uintptr_t)entry;
#elif defined(__aarch64__)
    /* x19..x29, x30 LR. SP remains 16-byte aligned. */
    const unsigned words=12U;
    if(top-bottom<words*8U+16U)return false;
    uint64_t *saved=(uint64_t *)(top-words*8U);
    for(unsigned i=0;i<words;i++)saved[i]=0;
    saved[11]=(uint64_t)(uintptr_t)entry;
#else
#error unsupported kernel continuation architecture
#endif
    ctx->saved_sp=(uint64_t)(uintptr_t)saved;
    return true;
}
/* Actual native QEMU boot test of suspended/resumed live kernel C frames;
 * the syscall-boundary scheduler stays unchanged during this stage. */
extern void nas_fail(const char *) __attribute__((noreturn));
extern void nas_print(const char *);
static struct nas_kernel_context boot_main_ctx,boot_worker_ctx;
static _Alignas(16) uint8_t boot_worker_stack[8192];
static volatile uint64_t boot_phase;
static void boot_worker(void) {
    volatile uint64_t alive=UINT64_C(0x4e61536565644b31);
    boot_phase=1;
    nas_arch_kernel_swap(&boot_worker_ctx,&boot_main_ctx);
    if(alive!=UINT64_C(0x4e61536565644b31) || boot_phase!=2)
        nas_fail("M91 kernel stack frame not restored");
    boot_phase=3;
    nas_arch_kernel_swap(&boot_worker_ctx,&boot_main_ctx);
    nas_fail("M91 returned to retired bootstrap context");
}
void nas_kernel_context_boot_selftest(void) {
    boot_phase=0;
    nas_kernel_context_reset(&boot_main_ctx);
    if(!nas_kernel_context_prepare(&boot_worker_ctx,boot_worker_stack,
                                   sizeof(boot_worker_stack),boot_worker))
        nas_fail("M91 cannot initialize continuation stack");
    nas_arch_kernel_swap(&boot_main_ctx,&boot_worker_ctx);
    if(boot_phase!=1)nas_fail("M91 first kernel context switch failed");
    boot_phase=2;
    nas_arch_kernel_swap(&boot_main_ctx,&boot_worker_ctx);
    if(boot_phase!=3)nas_fail("M91 kernel C continuation lost");
    nas_print("[M91 KCTX PASS] native kernel C frame resume\n");
}
