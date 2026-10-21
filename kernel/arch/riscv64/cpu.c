#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"
#include "../../task.h"
#include "../context.h"
/* Limine paging mode values 0/1/2 -> Sv39/Sv48/Sv57.
 * satp.MODE values 8/9/10. Refuse unknown or mismatched modes. */
uint64_t nas_riscv_satp_read(void) {
    uint64_t value;
    __asm__ volatile ("csrr %0, satp" : "=r"(value));
    return value;
}
unsigned nas_riscv_page_table_levels(uint64_t mode) {
    switch (mode) {
    case 0: return 3;
    case 1: return 4;
    case 2: return 5;
    default: return 0;
    }
}
bool nas_riscv_validate_limine_mode(uint64_t mode) {
    if (!nas_riscv_page_table_levels(mode)) return false;
    return (nas_riscv_satp_read() >> 60) == (mode + 8);
}
/* Strict hardware-derived user VA limit: Sv39, Sv48, Sv57 as negotiated
 * with Limine. Do not promise 48-bit user mappings on an Sv39 CPU. */
uint64_t nas_arch_user_limit(void) {
    uint64_t mode = nas_riscv_satp_read() >> 60;
    if (mode < 8 || mode > 10) return 0;
    return 1ULL << (11 + (unsigned)(mode - 5) * 9);
}
bool nas_arch_user_nx_supported(void) {
    uint64_t mode = nas_riscv_satp_read() >> 60;
    return mode == 8 || mode == 9 || mode == 10; /* leaf X is mandatory */
}
void nas_arch_halt(void) {
    for (;;) __asm__ volatile ("wfi");
}

// Real RV64 full-kernel CPU context; not used by M0-only smoke.
void nas_arch_prepare_user_cpu(void) {
    uint64_t misa;
    __asm__ volatile("csrr %0, misa" : "=r"(misa));
    if(!(misa & (1ULL << ('D'-'A'))) ||
       !(misa & (1ULL << ('F'-'A')))) {
        extern void nas_fail(const char *);
        nas_fail("RV64 Linux userspace requires hardware F/D");
    }
    /* Enable floating point instructions for the kernel save/restore path
     * and preserve user double-precision state across cooperative switches. */
    uint64_t fs=3ULL<<13;
    __asm__ volatile("csrs sstatus,%0" : : "r"(fs) : "memory");
}
extern void nas_rv_full_trap(void);
extern char kernel_stack_top;
void nas_arch_enable_user_traps(void) {
    uintptr_t addr=(uintptr_t)nas_rv_full_trap;
    uintptr_t stack=(uintptr_t)&kernel_stack_top;
    __asm__ volatile("csrw stvec,%0\ncsrw sscratch,%1"
                   : : "r"(addr),"r"(stack) : "memory");
}
uint64_t nas_arch_cycle_counter(void) {
    uint64_t cycles;
    __asm__ volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}
void nas_arch_set_user_tls(uint64_t address) {
    /* Per-task TP is restored from its saved trap frame on every sret.
     * The generic process state owns this value, not the current kernel TP. */
    if (nas_current_task)
        nas_arch_context_set_tls(&nas_current_task->arch,address);
}
