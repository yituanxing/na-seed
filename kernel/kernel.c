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
#ifndef NASEED_PORT_M0
#include "drivers/display/fbdev.h"
#endif
#include "mm/frame.h"
#ifndef NASEED_PORT_M0
#include "mm/heap.h"
#endif
#include "random.h"
#include "mm/user_space.h"
#include "init/main.h"
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
#if !defined(__x86_64__)
#error "H0 physical PC discovery currently supports only x86-64"
#endif
#include "platform/firmware_acpi.h"
#include "platform/x86_h0_pci.h"
#ifdef NAS_ENABLE_H1_ECAM_DISCOVERY
#include "platform/x86_h1_ecam.h"
#endif
#endif

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
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
/* Limine base revision 6 returns an HHDM-mapped virtual RSDP pointer.
 * Firmware tables are parsed read-only; this request never starts AML or
 * probes a disk, PCI BAR, interrupt controller, or power-management port. */
USED_REQUEST static volatile struct limine_rsdp_request h0_rsdp_req = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0
};
#endif
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

const struct limine_file *nas_user_module(void);

#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
static bool h0_acpi_memory_type(uint64_t type) {
    return type==LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
           type==LIMINE_MEMMAP_ACPI_NVS ||
           type==LIMINE_MEMMAP_RESERVED_MAPPED;
}

/* ACPI contents are never trusted to identify an accessible address. Every
 * physical read is checked against the Limine memory map before HHDM access.
 * Requiring a single contiguous allowed descriptor safely rejects ambiguous
 * firmware maps and any physical MMIO or ordinary RAM address. */
static size_t h0_acpi_mapped_extent(uint64_t physical, size_t requested) {
    if (!mmap_req.response || !requested ||
        physical>UINT64_MAX-(uint64_t)requested)return 0;
    for(uint64_t i=0;i<mmap_req.response->entry_count;i++) {
        const struct limine_memmap_entry *entry=mmap_req.response->entries[i];
        if(!entry || !h0_acpi_memory_type(entry->type) ||
           physical<entry->base || physical-entry->base>=entry->length)
            continue;
        uint64_t available=entry->length-(physical-entry->base);
        if(available<requested)return 0;
        return available>4096U ? 4096U : (size_t)available;
    }
    return 0;
}

static const void *h0_acpi_physical_map(void *context,uint64_t physical,
                                        size_t bytes) {
    (void)context;
    if(!h0_acpi_mapped_extent(physical,bytes))return NULL;
    uint64_t hhdm=nas_hhdm_offset();
    if(physical>UINTPTR_MAX-hhdm)return NULL;
    return (const void *)(uintptr_t)(physical+hhdm);
}

static bool h0_acpi_diagnostic(struct nas_acpi_discovery *out) {
    nas_print("[H0] read-only firmware discovery (no driver binding)\n");
    if(!out)return false;
    if(!h0_rsdp_req.response || !h0_rsdp_req.response->address) {
        nas_print("[H0 ACPI] no Limine RSDP response\n");
        return false;
    }
    uint64_t virtual_address=(uint64_t)(uintptr_t)h0_rsdp_req.response->address;
    uint64_t hhdm=nas_hhdm_offset();
    if(virtual_address<hhdm) {
        nas_print("[H0 ACPI] RSDP not in Limine HHDM\n");
        return false;
    }
    uint64_t physical=virtual_address-hhdm;
    size_t available=h0_acpi_mapped_extent(physical,20U);
    if(!available) {
        nas_print("[H0 ACPI] RSDP outside allowed firmware memory\n");
        return false;
    }
    struct nas_acpi_discovery acpi={0};
    if(nas_acpi_probe(h0_rsdp_req.response->address,available,
                      h0_acpi_physical_map,NULL,&acpi)) {
        nas_print("[H0 ACPI] invalid/inaccessible tables; no MMIO mapped\n");
        return false;
    }
    nas_print("[H0 ACPI] XSDT=");nas_dec(acpi.used_xsdt);
    nas_print(" tables=");nas_dec(acpi.table_count);
    nas_print(" MADT=");nas_dec(acpi.madt_present);
    nas_print(" MCFG windows=");nas_dec(acpi.mcfg_count);
    nas_print("\n");
    for(unsigned i=0;i<acpi.mcfg_count;i++) {
        nas_print("[H0 MCFG] ECAM=");nas_hex(acpi.mcfg[i].ecam_base);
        nas_print(" segment=");nas_dec(acpi.mcfg[i].segment);
        nas_print(" buses=");nas_dec(acpi.mcfg[i].bus_start);
        nas_print("..");nas_dec(acpi.mcfg[i].bus_end);
        nas_print("\n");
    }
    *out=acpi;
    return true;
}
#ifdef NAS_ENABLE_H1_ECAM_DISCOVERY
/* ECAM is MMIO, NOT ACPI table memory. Only a full reserved descriptor is
 * eligible for a new UC/RO PTE. Never use the Limine HHDM as an MMIO alias. */
static bool h1_ecam_mmio_allowed(void *context, uint64_t physical,
                                  size_t bytes) {
    (void)context;
    if(!mmap_req.response || !bytes || physical>UINT64_MAX-(uint64_t)bytes)
        return false;
    for(uint64_t i=0;i<mmap_req.response->entry_count;i++) {
        const struct limine_memmap_entry *entry=mmap_req.response->entries[i];
        if(!entry || (entry->type!=LIMINE_MEMMAP_RESERVED &&
                      entry->type!=LIMINE_MEMMAP_RESERVED_MAPPED) ||
           physical<entry->base || physical-entry->base>=entry->length)
            continue;
        return (uint64_t)bytes<=entry->length-(physical-entry->base);
    }
    return false;
}
#endif
#endif

#ifndef NASEED_PORT_M0
/* HHDM translates physically adjacent frames into one contiguous arena.
 * Frame ownership is retained for the lifetime of the registered arena. */
static bool grow_heap_from_frames(size_t minimum_bytes, void *context) {
    (void)context;
    const uint64_t page_bytes=4096;
    uint64_t pages=(uint64_t)(minimum_bytes/page_bytes)+
                   (minimum_bytes%page_bytes!=0);
    if(!pages || pages>SIZE_MAX/page_bytes)return false;
    uint64_t physical=nas_frame_alloc_contiguous(pages);
    if(!physical)return false;
    size_t bytes=(size_t)(pages*page_bytes);
    uint64_t offset=nas_hhdm_offset();
    if(offset<=UINTPTR_MAX-physical &&
       nas_heap_add_arena((void *)(uintptr_t)(offset+physical),bytes))
        return true;
    /* The arena registry rejected the mapping; restore every page owner. */
    for(uint64_t i=0;i<pages;i++)(void)nas_frame_put(physical+i*page_bytes);
    return false;
}
#endif

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
#ifndef NASEED_PORT_M0
    /* Limine supplies a mapped kernel VA, not a PMM-owned RAM frame. An
     * absent/unsupported display remains optional on serial-only QEMU. */
    bool fb_ready=false;
    if(fb_req.response && fb_req.response->framebuffers) {
        uint64_t available=fb_req.response->framebuffer_count;
        if(available>16U)available=16U;
        for(uint64_t i=0;i<available && !fb_ready;i++) {
            const struct limine_framebuffer *fb=
                fb_req.response->framebuffers[i];
            if(!fb)continue;
            const struct nas_fb_boot_info info={
                .address=(volatile uint8_t *)fb->address,
                .width=fb->width,.height=fb->height,.pitch=fb->pitch,
                .bpp=fb->bpp,.memory_model=fb->memory_model,
                .red_size=fb->red_mask_size,.red_shift=fb->red_mask_shift,
                .green_size=fb->green_mask_size,.green_shift=fb->green_mask_shift,
                .blue_size=fb->blue_mask_size,.blue_shift=fb->blue_mask_shift
            };
            fb_ready=nas_fbdev_boot_init(&info);
            if(fb_ready) {
                nas_print("[FB] firmware framebuffer ready ");
                nas_dec(fb->width);nas_print("x");nas_dec(fb->height);
                nas_print(" pitch=");nas_dec(fb->pitch);
                nas_print(" bpp=");nas_dec(fb->bpp);
                nas_print(" r");nas_dec(fb->red_mask_size);
                nas_print("@");nas_dec(fb->red_mask_shift);
                nas_print(" g");nas_dec(fb->green_mask_size);
                nas_print("@");nas_dec(fb->green_mask_shift);
                nas_print(" b");nas_dec(fb->blue_mask_size);
                nas_print("@");nas_dec(fb->blue_mask_shift);
                nas_print("\n");
            }
        }
    }
    if(!fb_ready)nas_print("[FB] no usable firmware framebuffer\n");
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
    else nas_h0_serial_enable_framebuffer();
#endif
#endif
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
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
    struct nas_acpi_discovery h0_acpi={0};
    bool h0_acpi_ok=h0_acpi_diagnostic(&h0_acpi);
    struct nas_pci_scan_stats h0_pci_stats={0};
    if(nas_x86_h0_pci_inventory(&h0_pci_stats))
        nas_print("[H0 PCI] legacy config read failed; no drivers bound\n");
#ifdef NAS_ENABLE_H1_ECAM_DISCOVERY
    if(!h0_acpi_ok ||
       nas_x86_h1_ecam_verify(&h0_acpi,h1_ecam_mmio_allowed,NULL)) {
        nas_print("[H1 ECAM] unavailable or mismatch; no drivers bound\n");
#ifdef NAS_ENABLE_H3_QEMU_NVME_IDENTIFY
        nas_print("[H3 NVME FAIL] required QEMU admin test failed\n");
        nas_arch_halt();
#endif
    }
#else
    (void)h0_acpi_ok;
#endif
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
    nas_print("[H4 COMPLETE] QEMU-only disposable namespace read; no LBA writes\n");
#elif defined(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY)
    nas_print("[H3 COMPLETE] QEMU-only NVMe Identify; no namespace LBA I/O\n");
#else
    nas_print("[H0 COMPLETE] firmware inventory only; no physical disk I/O\n");
#endif
    nas_arch_halt();
#endif
#ifndef NASEED_PORT_M0
    nas_heap_set_grow_callback(grow_heap_from_frames,NULL);
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
