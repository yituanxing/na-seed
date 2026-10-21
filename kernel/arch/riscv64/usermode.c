#include <stdint.h>
#include "cpu.h"
#include "mmu.h"
#include "usermode.h"
#include "../../mm/frame.h"
#include "../../drivers/serial.h"

#define RV_USER_PC 0x00010000ULL
#define RV_USER_STACK 0x00020000ULL
#define RV_SATP_PPN_MASK ((1ULL << 44) - 1)
/* Single-core M0 diagnostic uses a dedicated supervisor trap stack.
 * No scheduler, nested trap support or independent kernel threads yet. */
__attribute__((aligned(16))) static unsigned char rv_user_trap_stack[4096];

static void rv_fatal(const char *message) __attribute__((noreturn));
static void rv_fatal(const char *message) {
    nas_print("[FAIL] RV64 real user mode: ");
    nas_print(message);
    nas_print("\n");
    nas_arch_halt();
}

void nas_rv_user_trap_c(uint64_t cause, uint64_t sstatus, uint64_t sepc,
                       uint64_t nr, uint64_t result) {
    /* RISC-V scause 8 is ECALL from User. SPP must be zero.
     * Linux asm-generic __NR_exit is 93, with code in a0. */
    if (cause != 8 || (sstatus & (1ULL << 8)) ||
        nr != 93 || result != 42 ||
        sepc < RV_USER_PC || sepc >= RV_USER_PC + NAS_RV_PAGE_SIZE) {
        nas_print("[RV64 TRAP] scause="); nas_hex(cause);
        nas_print(" sstatus="); nas_hex(sstatus);
        nas_print(" sepc="); nas_hex(sepc);
        nas_print(" a7="); nas_hex(nr);
        nas_print(" a0="); nas_hex(result);
        nas_print("\n");
        rv_fatal("unexpected trap / syscall identity");
    }
    nas_print("[PASS] RV64 hardware U-mode Linux exit(42) ecall via sret/satp\n");
    nas_arch_halt();
}

void nas_rv_user_smoke(uint64_t limine_mode) {
    unsigned levels = nas_riscv_page_table_levels(limine_mode);
    uint64_t active_satp = nas_riscv_satp_read();
    if (!levels || (active_satp >> 60) != (uint64_t)(levels + 5))
        rv_fatal("invalid active Sv mode");
    struct nas_rv_page_table pt = {0};
    if (!nas_rv_pt_init(&pt, levels)) rv_fatal("user root frame allocation");
    uint64_t code_pa = nas_frame_alloc();
    uint64_t stack_pa = nas_frame_alloc();
    if (!code_pa || !stack_pa || code_pa == stack_pa)
        rv_fatal("user code/stack physical frames");

    /* Full trap return tests hardware page walks through a *new* satp root:
     * share only existing higher-half kernel/boot HHDM mappings from Limine.
     * Never free, alter or reinterpret Limine-owned kernel page tables. */
    uint64_t prior_root_pa = (active_satp & RV_SATP_PPN_MASK) << 12;
    uint64_t hhdm = nas_hhdm_offset();
    const uint64_t *prior_root = (const uint64_t *)(uintptr_t)(hhdm + prior_root_pa);
    uint64_t *new_root = (uint64_t *)(uintptr_t)(hhdm + pt.root_pa);
    for (unsigned i = 256; i < 512; i++) new_root[i] = prior_root[i];

    if (!nas_rv_pt_map(&pt, RV_USER_PC, code_pa,
                       NAS_RV_PTE_U | NAS_RV_PTE_R | NAS_RV_PTE_X) ||
        !nas_rv_pt_map(&pt, RV_USER_STACK, stack_pa,
                       NAS_RV_PTE_U | NAS_RV_PTE_R | NAS_RV_PTE_W))
        rv_fatal("offline user code/stack page mappings");

    uint64_t code_size = (uint64_t)(nas_rv_user_end - nas_rv_user_start);
    if (!code_size || code_size > NAS_RV_PAGE_SIZE)
        rv_fatal("user machine code size invalid");
    unsigned char *code = (unsigned char *)(uintptr_t)(hhdm + code_pa);
    for (uint64_t i = 0; i < code_size; i++)
        code[i] = nas_rv_user_start[i];

    uint64_t got = 0, perms = 0;
    if (!nas_rv_pt_translate(&pt, RV_USER_PC, &got, &perms) ||
        got != code_pa || !(perms & NAS_RV_PTE_X) ||
        !nas_rv_pt_translate(&pt, RV_USER_STACK, &got, &perms) ||
        got != stack_pa || !(perms & NAS_RV_PTE_W))
        rv_fatal("user mapping verification");

    uint64_t new_satp = (active_satp & (15ULL << 60)) |
                        (pt.root_pa >> 12);
    uint64_t trap_sp = (uint64_t)(uintptr_t)
                       (rv_user_trap_stack + sizeof(rv_user_trap_stack));
    nas_print("[PORT] RV64 replacing boot satp root; real U-mode exit syscall\n");
    nas_rv_jump_user(new_satp, RV_USER_PC, RV_USER_STACK + 4080, trap_sp);
}
