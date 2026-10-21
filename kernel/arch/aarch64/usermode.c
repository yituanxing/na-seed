#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"
#include "mmu.h"
#include "usermode.h"
#include "../../mm/frame.h"
#include "../../drivers/serial.h"

#define ARM_USER_PC 0x00010000ULL
#define ARM_USER_STACK 0x00020000ULL
static uint64_t arm_user_entry;

static void fatal(const char *why) __attribute__((noreturn));
static void fatal(const char *why) {
    nas_print("[FAIL] ARM64 live EL0: ");
    nas_print(why);
    nas_print("\n");
    nas_arch_halt();
}
void nas_arm_user_trace_stage(uint64_t stage) {
    uint64_t spsel, el;
    __asm__ volatile ("mrs %0, SPSel" : "=r"(spsel));
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(el));
    nas_print("[ARM64 TRACE] stage=");
    nas_dec(stage);
    nas_print(" currentEL=");
    nas_hex(el);
    nas_print(" spsel=");
    nas_hex(spsel);
    nas_print("\n");
}
void nas_arm_user_last_el1_trace(void) {
    uint64_t spsel, sp_el1, tcr, mair;
    __asm__ volatile ("mrs %0, SPSel" : "=r"(spsel));
    __asm__ volatile ("mrs %0, SP_EL1" : "=r"(sp_el1));
    __asm__ volatile ("mrs %0, TCR_EL1" : "=r"(tcr));
    __asm__ volatile ("mrs %0, MAIR_EL1" : "=r"(mair));
    nas_print("[ARM64 TRACE] EL1 before ERET spsel=");
    nas_hex(spsel); nas_print(" sp_el1="); nas_hex(sp_el1);
    nas_print(" tcr="); nas_hex(tcr);
    nas_print(" mair="); nas_hex(mair);
    nas_print("\n");
}
void nas_arm_user_bad_vector(void) {
    fatal("unexpected exception vector");
}
void nas_arm_user_trap_c(uint64_t esr, uint64_t elr, uint64_t spsr,
                         uint64_t nr, uint64_t arg0) {
    /* ESR_EL1.EC=0x15: SVC from AArch64 lower EL (EL0). Linux syscall
     * nr=x8, return/arg0=x0. SPSR_EL1 M[3:0] must be EL0t (0).
     * SVC advances ELR to instruction immediately after the SVC. */
    if ((esr >> 26) != 0x15 || (esr & 0xffff) != 0 ||
        (spsr & 15) != 0 || nr != 93 || arg0 != 42 ||
        elr < arm_user_entry || elr >= arm_user_entry + NAS_ARM_PAGE_SIZE) {
        nas_print("[ARM64 TRAP] esr="); nas_hex(esr);
        nas_print(" elr="); nas_hex(elr);
        nas_print(" spsr="); nas_hex(spsr);
        nas_print(" x8="); nas_hex(nr);
        nas_print(" x0="); nas_hex(arg0);
        nas_print("\n");
        fatal("unexpected exception, mode or syscall");
    }
    nas_print("[PASS] AArch64 hardware EL0 Linux exit(42) SVC via ERET/TTBR0\n");
    nas_arch_halt();
}
void nas_arm_user_smoke(void) {
    /* Use the same proven allocator-backed TTBR0 PTE implementation as
     * the offline tests, but now prove real hardware page walks at EL0. */
    if (!nas_arm64_el1_4k_mmu_active())
        fatal("not running EL1 4KiB MMU");
    struct nas_arm_pt p = {0};
    if (!nas_arm_pt_init(&p, 3)) fatal("TTBR0 root allocation");
    uint64_t code_pa = nas_frame_alloc();
    uint64_t stack_pa = nas_frame_alloc();
    if (!code_pa || !stack_pa || code_pa == stack_pa)
        fatal("user code/stack physical allocation");
    if (!nas_arm_pt_map(&p, ARM_USER_PC, code_pa,
                        NAS_ARM_USER | NAS_ARM_R | NAS_ARM_X) ||
        !nas_arm_pt_map(&p, ARM_USER_STACK, stack_pa,
                        NAS_ARM_USER | NAS_ARM_R | NAS_ARM_W))
        fatal("user code/stack PTE map");
    uint64_t size = (uint64_t)(nas_arm_user_code_end - nas_arm_user_code);
    if (!size || size > NAS_ARM_PAGE_SIZE) fatal("user code size");
    uint8_t *dst = (uint8_t *)(uintptr_t)(nas_hhdm_offset() + code_pa);
    for (uint64_t i = 0; i < size; ++i) dst[i] = nas_arm_user_code[i];
    /* Code copied via HHDM may remain in D-cache: synchronize I-cache
     * before EL0 instruction fetch. Inner-shareable barriers are required. */
    for (uint64_t i = 0; i < size; i += 64)
        __asm__ volatile ("dc cvau, %0" :: "r"(dst + i) : "memory");
    __asm__ volatile ("dsb ish" ::: "memory");
    for (uint64_t i = 0; i < size; i += 64)
        __asm__ volatile ("ic ivau, %0" :: "r"(dst + i) : "memory");
    __asm__ volatile ("dsb ish\n\tisb" ::: "memory");

    uint64_t pa = 0, access = 0;
    if (!nas_arm_pt_translate(&p, ARM_USER_PC, &pa, &access) ||
        pa != code_pa || !(access & NAS_ARM_X) ||
        !nas_arm_pt_translate(&p, ARM_USER_STACK, &pa, &access) ||
        pa != stack_pa || !(access & NAS_ARM_W))
        fatal("user hardware input mappings");
    arm_user_entry = ARM_USER_PC;
    nas_print("[PORT] AArch64 entering live TTBR0 + EL0 via ERET\n");
    nas_arm_enter_el0(p.root_pa, arm_user_entry,
                      ARM_USER_STACK + NAS_ARM_PAGE_SIZE - 16);
}
