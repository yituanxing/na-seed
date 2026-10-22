#include <stdint.h>
#include <stdbool.h>
#include "../console.h"
#include "../../mm/frame.h"

/* QEMU virt supplies an ARM PL011 at physical 0x09000000. Before memory
 * initialization, diagnostics MUST retain the existing semihost SYS_WRITEC
 * path. After frames_init, explicitly install a Device-nGnRnE mapping in
 * the live TTBR1 kernel page tables. Limine base revision 6 does not HHDM
 * map arbitrary MMIO; casting 0x09000000+HHDM would fault or cache IO.
 *
 * The chosen virtual IO page is kernel-only and far from executable
 * segments. On real hardware, a DT/ACPI-discovered PL011 address and a
 * general kernel ioremap service should replace this QEMU virt platform
 * constant. No hardcoded device addresses enter generic TTY, FS, or Task.
 */
#define PL011_PHYS 0x09000000ULL
#define PL011_VIRT 0xffffffffe0000000ULL
#define PHYS_MASK  0x0000fffffffff000ULL
#define DEVICE_IDX 6ULL
#define REG_DR 0x00
#define REG_FR 0x18
#define REG_CR 0x30
static volatile uint32_t *uart;
static void semi_putc(char ch) {
    char x = ch;
    register uint64_t x0 __asm__("x0") = 0x03;
    register uint64_t x1 __asm__("x1") = (uint64_t)(uintptr_t)&x;
    __asm__ volatile ("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
}
void nas_arch_console_init(void) { uart = 0; }
static uint32_t uart_reg(unsigned off) {
    return uart[off / 4];
}
static void uart_set(unsigned off, uint32_t val) {
    uart[off / 4] = val;
}
void nas_arch_console_putc(char c) {
    if (!uart) { semi_putc(c); return; }
    unsigned bound = 1000000;
    while ((uart_reg(REG_FR) & (1U << 5)) && bound--) { }
    if (!(uart_reg(REG_FR) & (1U << 5)))
        uart_set(REG_DR, (uint8_t)c);
}
/* Device-nGnRnE mapping of one supervisor-only MMIO page. Used by
 * PL011 console and the QEMU virt GICv2 hardware timer controller.
 * This shared implementation owns the MAIR and upper EL1 page walk;
 * architecture-independent driver/TTY/scheduler code knows no PA/VA.
 * Call only after nas_frames_init while kernel TTBR1 is live.
 */
bool nas_arm_map_io_page(uint64_t physical, uint64_t virt) {
    if((physical & 4095U) || (virt & 4095U)) return false;
    uint64_t tcr, ttbr;
    __asm__ volatile ("mrs %0,tcr_el1" : "=r"(tcr));
    __asm__ volatile ("mrs %0,ttbr1_el1" : "=r"(ttbr));
    if ((tcr >> 30 & 3ULL) != 2) return false; /* TG1=4KiB */
    unsigned t1sz = (unsigned)((tcr >> 16) & 63U);
    if (t1sz != 16 && t1sz != 25) return false; /* 48-/39-bit */
    unsigned levels = t1sz == 16 ? 4 : 3;
    uint64_t root = ttbr & PHYS_MASK;
    if (!root) return false;

    uint64_t pa = root;
    for (unsigned level = levels; level > 1; level--) {
        unsigned shift = 12 + 9 * (level - 1);
        unsigned index = (unsigned)((virt >> shift) & 511U);
        volatile uint64_t *table = (volatile uint64_t *)(uintptr_t)
                (nas_hhdm_offset() + pa);
        uint64_t desc = table[index];
        if (desc == 0) {
            uint64_t next = nas_frame_alloc();
            if (!next) return false;
            table[index] = next | 3ULL; /* EL1 table descriptor */
            desc = table[index];
        }
        /* Reusing an existing section/superpage as a page table would
         * reinterpret a block as a pointer and corrupt firmware state. */
        if ((desc & 3ULL) != 3ULL) return false;
        pa = desc & PHYS_MASK;
        if (!pa) return false;
    }
    volatile uint64_t *leaf = (volatile uint64_t *)(uintptr_t)
            (nas_hhdm_offset() + pa);
    unsigned index = (unsigned)((virt >> 12) & 511U);
    if (leaf[index]) return false; /* never overwrite an existing mapping */

    /* MAIR index 6 is unused by Limine; map UART as Device-nGnRnE.
     * EL1-only RW, access flag, privileged/user execute never. */
    uint64_t mair;
    __asm__ volatile ("mrs %0,mair_el1" : "=r"(mair));
    mair &= ~(255ULL << (DEVICE_IDX * 8));
    __asm__ volatile ("msr mair_el1,%0" : : "r"(mair) : "memory");
    leaf[index] = physical | 3ULL | (DEVICE_IDX << 2) |
                  (1ULL << 10) | (1ULL << 53) | (1ULL << 54);
    __asm__ volatile ("dsb ishst\n tlbi vmalle1is\n dsb ish\n isb"
                      : : : "memory");
    return true;
}
/* The console remains a special case of the same kernel-only IO mapper. */
bool nas_arch_console_enable_input(void) {
    if(!nas_arm_map_io_page(PL011_PHYS,PL011_VIRT)) return false;
    uart = (volatile uint32_t *)(uintptr_t)PL011_VIRT;
    uint32_t cr = uart_reg(REG_CR);
    uart_set(REG_CR, cr | 0x301U); /* UARTEN | TXE | RXE */
    return true;
}
bool nas_arch_console_has_rx(void) { return uart != 0; }
bool nas_arch_console_getc(uint8_t *byte) {
    if (!uart || !byte || (uart_reg(REG_FR) & (1U << 4))) return false;
    *byte = (uint8_t)uart_reg(REG_DR);
    return true;
}
/* Early semihosting uses plain LF; active PL011 needs CRLF for boot
 * diagnostics, preserving existing exact line-based shell regression. */
void nas_arch_console_log_newline(void) {
    if (uart) nas_arch_console_putc('\r');
}
