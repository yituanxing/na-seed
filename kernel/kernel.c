#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/arch.h"
#if defined(NASEED_PORT_M0) && defined(__riscv)
#include "arch/riscv64/cpu.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/usermode.h"
#elif defined(NASEED_PORT_M0) && defined(__aarch64__)
#include "arch/aarch64/cpu.h"
#include "arch/aarch64/mmu.h"
#endif
#include "drivers/serial.h"
#include "mm/frame.h"
#include "random.h"
#include "mm/user_space.h"
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
/* RTC-derived wall-clock calendar is an OPTIONAL Limine handoff.
 * Never synthesize UNIX time from a monotonic hardware counter alone. */
USED_REQUEST static volatile struct limine_date_at_boot_request date_req = {
    .id = LIMINE_DATE_AT_BOOT_REQUEST_ID, .revision = 0
};
/* Best-effort firmware seed; do not promise certified hardware entropy. */
USED_REQUEST static volatile struct limine_entropy_request entropy_req = {
    .id = LIMINE_ENTROPY_REQUEST_ID, .revision = 0, .value_count = 4
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

const struct limine_file *nas_user_module(void);

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
    if (!a || !b || a == b || !nas_frame_put(a) || !nas_frame_put(b) ||
        nas_free_frames() != before) {
        nas_print("[FAIL] frame allocator smoke\n");
        nas_arch_halt();
    }
    nas_print("[PASS] M0 memory allocator\n");
    nas_print("[PASS] M0 boot / serial / memory\n");
#ifndef NASEED_PORT_M0
    bool entropy_ready = entropy_req.response &&
        nas_random_seed(entropy_req.response->values,
                        entropy_req.response->value_count);
    nas_print(entropy_ready ?
        "[RANDOM] Limine best-effort entropy seeded ChaCha20\n" :
        "[RANDOM] no usable boot entropy; getrandom returns ENOSYS\n");
#endif
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
    if (!nas_space_regression()) {
        nas_print("[FAIL] shared RV64 user VM clone/protect/free\n");
        nas_arch_halt();
    }
    nas_print("[PASS] shared user VM clone/protect/unmap/reclaim RV64\n");
    nas_print("[PASS] riscv64 M0 Limine/SBI/frame allocator\n");
    const struct limine_file *app = nas_user_module();
    if (!app || !app->address || !app->size) {
        nas_print("[FAIL] RV64 missing real ELF module\n");
        nas_arch_halt();
    }
    nas_rv_user_smoke(paging_req.response->mode,
                      (const uint8_t *)app->address, app->size);
#elif defined(__aarch64__)
    /* AArch64 needs a different MMU regime; never assume RISC-V SATP. */
    if (!nas_arm64_el1_4k_mmu_active()) {
        nas_print("[FAIL] AArch64 expected EL1 4KiB TTBR1 regime\n");
        nas_arch_halt();
    }
    nas_print("[PORT] AArch64 EL1 stage-1 MMU 4KiB confirmed\n");
    if (!nas_arm_pt_smoke()) {
        nas_print("[FAIL] AArch64 39/48-bit user PTE selftest\n");
        nas_arch_halt();
    }
    nas_print("[PASS] AArch64 39/48-bit user PTE map/query/unmap/reclaim\n");
    if (!nas_space_regression()) {
        nas_print("[FAIL] shared AArch64 user VM clone/protect/free\n");
        nas_arch_halt();
    }
    nas_print("[PASS] shared user VM clone/protect/unmap/reclaim AArch64\n");
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
/* RTC may be unavailable/stale on virtual firmware. An OPTIONAL trusted
 * Limine boot UTC clock lets the launcher supply real UNIX wall time:
 *
 *   naseed.realtime=<UTC epoch seconds> naseed.argv=<program> [args...]
 *
 * This prefix is boot metadata, not a userspace argument. With no prefix,
 * preserve the original firmware RTC handoff. Malformed prefixes remain
 * unstripped so the existing argv parser fails closed. All ISA share this. */
static const char *boot_cmdline_and_time(int64_t *epoch) {
    const char *cmd = cmdline_req.response ? cmdline_req.response->cmdline : 0;
    static const char key[] = "naseed.realtime=";
    static const char argv_key[] = "naseed.argv=";
    if (!cmd) return 0;
    unsigned i = 0;
    while (key[i] && cmd[i] == key[i]) ++i;
    if (key[i]) return cmd;
    const char *p = cmd + i;
    uint64_t value = 0;
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9') {
        uint64_t digit = (uint64_t)(*p++ - '0');
        if (value > (UINT64_C(4102444800) - digit) / 10) return cmd;
        value = value * 10 + digit;
        ++digits;
    }
    if (!digits || value < UINT64_C(946684800) || *p != ' ')
        return cmd;
    while (*p == ' ') ++p;
    i = 0;
    while (argv_key[i] && p[i] == argv_key[i]) ++i;
    if (argv_key[i]) return cmd;
    if (epoch) *epoch = (int64_t)value;
    return p;
}
const char *nas_cmdline(void) {
    return boot_cmdline_and_time(0);
}

/* Shared monotonic timekeeper anchors to either launcher UTC or Limine RTC. */
int64_t nas_boot_date_at_boot(void) {
    int64_t epoch = -1;
    (void)boot_cmdline_and_time(&epoch);
    if (epoch >= 0) {
        nas_print("[CLOCK] trusted boot UTC epoch=");
        nas_dec((uint64_t)epoch);
        nas_print("\n");
        return epoch;
    }
    return date_req.response ? date_req.response->timestamp : -1;
}
