#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/arch.h"
#if defined(NASEED_PORT_M0) && defined(__riscv)
#include "arch/riscv64/cpu.h"
#include "arch/riscv64/mmu.h"
#elif defined(NASEED_PORT_M0) && defined(__aarch64__)
#include "arch/aarch64/cpu.h"
#endif
#include "drivers/serial.h"
#include "mm/frame.h"
#include "init/main.h"

/*
 * NaSeed M0: Limine handoff + real COM1 UART + physical frame bitmap.
 * Future ELF/syscall support must be driven by real userspace execution.
 * Bootloader reclaimable memory is NOT put into the frame allocator.
 */
#define USED_REQUEST __attribute__((used, section(".limine_requests")))
USED_REQUEST static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(6);
USED_REQUEST static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_memmap_request mmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_module_request module_req = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};
#if defined(NASEED_PORT_M0) && defined(__riscv)
USED_REQUEST static volatile struct limine_paging_mode_request paging_req = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID, .revision = 0,
    .mode = LIMINE_PAGING_MODE_RISCV_SV48,
    .max_mode = LIMINE_PAGING_MODE_RISCV_SV57,
    .min_mode = LIMINE_PAGING_MODE_RISCV_SV39
};
#endif
/* Same Limine executable-cmdline feature as NaOS boot_get_cmdline(). */
USED_REQUEST static volatile struct limine_executable_cmdline_request cmdline_req = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0
};
USED_REQUEST static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

void kmain(void) {
    nas_serial_init();
    nas_print("[BOOT] NaSeed M0 kernel entered\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision) ||
        !hhdm_req.response || !mmap_req.response ||
        !mmap_req.response->entries || !mmap_req.response->entry_count) {
        nas_print("[FAIL] Limine handoff\n");
        nas_arch_halt();
    }
    nas_print("[BOOT] memory map entries=");
    nas_dec(mmap_req.response->entry_count);
    nas_print(" modules=");
    nas_dec(module_req.response ? module_req.response->module_count : 0);
    nas_print("\n");
    if (!nas_frames_init(mmap_req.response, hhdm_req.response->offset)) {
        nas_print("[FAIL] frame allocator init\n");
        nas_arch_halt();
    }
    uint64_t before = nas_free_frames();
    nas_print("[MM] available frames=");
    nas_dec(before);
    nas_print("\n");
    uint64_t a = nas_frame_alloc(), b = nas_frame_alloc();
    if (!a || !b || a == b || !nas_frame_free(a) || !nas_frame_free(b) ||
        nas_free_frames() != before) {
        nas_print("[FAIL] frame allocator smoke\n");
        nas_arch_halt();
    }
    nas_print("[PASS] M0 memory allocator\n");
    nas_print("[PASS] M0 boot / serial / memory\n");
#ifdef NASEED_PORT_M0
#if defined(__riscv)
    /* Preserve the proven RV64 Limine + all three page-table depth tests. */
    if (!paging_req.response ||
        !nas_riscv_validate_limine_mode(paging_req.response->mode)) {
        nas_print("[FAIL] RV64 Limine paging mode / satp mismatch\n");
        nas_arch_halt();
    }
    nas_print("[PORT] riscv64 paging levels=");
    nas_dec(nas_riscv_page_table_levels(paging_req.response->mode));
    nas_print("\n");
    if (!nas_rv_pt_smoke()) {
        nas_print("[FAIL] RV64 Sv39/Sv48/Sv57 page-table selftest\n");
        nas_arch_halt();
    }
    nas_print("[PASS] RV64 Sv39/Sv48/Sv57 user PTE map/query/unmap/reclaim\n");
    nas_print("[PASS] riscv64 M0 Limine/SBI/frame allocator\n");
    nas_arch_halt();
#elif defined(__aarch64__)
    /* AArch64 needs a different MMU regime; never assume RISC-V SATP. */
    if (!nas_arm64_el1_4k_mmu_active()) {
        nas_print("[FAIL] AArch64 expected EL1 4KiB TTBR1 regime\n");
        nas_arch_halt();
    }
    nas_print("[PORT] AArch64 EL1 stage-1 MMU 4KiB confirmed\n");
    nas_print("[PASS] aarch64 M0 Limine/UEFI/semihost/frame allocator\n");
    nas_arch_halt();
#else
#error "Unimplemented M0 architecture"
#endif
#else
    nas_init_userspace();
#endif
    nas_arch_halt();
}

const struct limine_file *nas_module(uint64_t index) {
    if (!module_req.response || index >= module_req.response->module_count) return 0;
    return module_req.response->modules[index];
}
const struct limine_file *nas_user_module(void) { return nas_module(0); }
const char *nas_cmdline(void) {
    return cmdline_req.response ? cmdline_req.response->cmdline : 0;
}
