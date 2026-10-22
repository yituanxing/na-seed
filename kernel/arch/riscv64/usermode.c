#include <stdint.h>
#include "cpu.h"
#include "mmu.h"
#include "usermode.h"
#include "../../task/elf_user.h"
#include "../linux_syscall_abi.h"
#include "../../mm/frame.h"
#include "../../mm/user_space.h"
#include "../../drivers/serial.h"

#define RV_USER_STACK 0x00020000ULL
static uint64_t rv_elf_entry;
/* SATP and supervisor mappings are now inherited by shared user-space. */
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
        nas_linux_decode_syscall(nr) != NAS_SC_exit || result != 42 ||
        sepc < rv_elf_entry || sepc >= rv_elf_entry + NAS_RV_PAGE_SIZE) {
        nas_print("[RV64 TRAP] scause="); nas_hex(cause);
        nas_print(" sstatus="); nas_hex(sstatus);
        nas_print(" sepc="); nas_hex(sepc);
        nas_print(" a7="); nas_hex(nr);
        nas_print(" a0="); nas_hex(result);
        nas_print("\n");
        rv_fatal("unexpected trap / syscall identity");
    }
    nas_print("[PASS] RV64 hardware U-mode Linux exit(42) ecall via sret/satp\n");
    nas_print("[PASS] RV64 real external ELF64 PT_LOAD/BSS executed in U-mode\n");
    nas_arch_halt();
}

void nas_rv_user_smoke(uint64_t limine_mode, const uint8_t *elf, uint64_t size) {
    unsigned levels = nas_riscv_page_table_levels(limine_mode);
    uint64_t active_satp = nas_riscv_satp_read();
    if (!levels || (active_satp >> 60) != (uint64_t)(levels + 5))
        rv_fatal("invalid active Sv mode");
    struct nas_user_space space = {0};
    if (!nas_space_new(&space, levels))
        rv_fatal("shared user address space create");
    if (!nas_space_map_page(&space, RV_USER_STACK, true))
        rv_fatal("shared user stack map");
    struct nas_rv_page_table pt = {space.root_pa, space.levels};
    uint64_t stack_pa = 0;
    if (!nas_space_lookup(&space, RV_USER_STACK, &stack_pa, 0))
        rv_fatal("shared stack translate");
    /* Workload bounds only. No RISC-V-only ELF loader or segment mapper. */
    const struct nas_user_elf_limits elf_limits={
        .min_va=0x10000ULL,.max_va=RV_USER_STACK,
        .max_image_bytes=1ULL<<20,.max_phnum=16,.max_pages=32
    };
    if (!nas_user_elf_load(&space, elf, size, &elf_limits, &rv_elf_entry))
        rv_fatal("shared ELF64 PT_LOAD/BSS loader");
    uint64_t got = 0, perms = 0;
    if (!nas_rv_pt_translate(&pt, rv_elf_entry, &got, &perms) ||
        !(perms & NAS_RV_PTE_X) ||
        !nas_rv_pt_translate(&pt, RV_USER_STACK, &got, &perms) ||
        got != stack_pa || !(perms & NAS_RV_PTE_W))
        rv_fatal("user ELF executable entry / stack permissions");

    uint64_t new_satp = (active_satp & (15ULL << 60)) |
                        (space.root_pa >> 12);
    uint64_t trap_sp = (uint64_t)(uintptr_t)
                       (rv_user_trap_stack + sizeof(rv_user_trap_stack));
    nas_print("[PORT] RV64 Limine module ELF64 mapped; switching satp into U-mode\n");
    nas_rv_jump_user(new_satp, rv_elf_entry, RV_USER_STACK + 4080, trap_sp);
}
