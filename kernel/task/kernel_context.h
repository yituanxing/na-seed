#ifndef NASEED_KERNEL_CONTEXT_H
#define NASEED_KERNEL_CONTEXT_H
#include <stdint.h>
#include <stdbool.h>
#include "wait_queue.h"
/* Resumable kernel C continuation: native callee-saved registers and saved
 * stack/return address. This is independent of user syscall/trap state. */
struct nas_kernel_context { uint64_t saved_sp; };
static inline void nas_kernel_context_reset(struct nas_kernel_context *ctx) {
    if(ctx)ctx->saved_sp=0;
}
/* The same native continuation and intrusive wait ownership is embedded by
 * Linux user Tasks and kernel workers. Scheduling policy may differ, but
 * context/queue lifetime must not have two incompatible representations. */
struct nas_kernel_execution {
    struct nas_kernel_context cpu;
    struct nas_wait_entry wait;
};
static inline void nas_kernel_execution_reset(struct nas_kernel_execution *exec) {
    if(!exec)return;
    nas_kernel_context_reset(&exec->cpu);
    nas_wait_entry_reset(&exec->wait);
}
/* Entry must never return; initial SP is ABI aligned for each ISA. */
bool nas_kernel_context_prepare(struct nas_kernel_context *ctx,void *stack,
                                uint64_t bytes,void (*entry)(void));
/* Caller must arrange MM/TLS/FPU, interrupt masking and Task ownership.
 * A kernel C swap cannot be used as the complete scheduler by itself. */
void nas_arch_kernel_swap(struct nas_kernel_context *from,
                          const struct nas_kernel_context *to);
void nas_kernel_context_boot_selftest(void);
#endif
