#ifndef NASEED_X86_QEMU_HW_CONTRACT_H
#define NASEED_X86_QEMU_HW_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

/* QEMU-only H11-H15 hardware acceptance bridge. Neither function qualifies
 * an arbitrary physical device, maps PCI BARs, or establishes interrupt/DMA
 * safety. A future platform PCI resource owner should replace this fixture
 * specific code without changing xHCI class-driver semantics. */
static inline bool nas_x86_qemu_fw_cfg_signature(void) {
    static const char expected[4] = {'Q', 'E', 'M', 'U'};
    const uint16_t select_port = 0x510U, data_port = 0x511U;
    __asm__ volatile("outw %0,%1" :: "a"((uint16_t)0), "Nd"(select_port)
                     : "memory");
    for (unsigned i = 0; i < sizeof(expected); ++i) {
        uint8_t ch = 0;
        __asm__ volatile("inb %1,%0" : "=a"(ch) : "Nd"(data_port)
                         : "memory");
        if (ch != (uint8_t)expected[i]) return false;
    }
    return true;
}

/* CF8/CFC mechanism #1. Call only with the caller's existing IRQ exclusion.
 * Status bits in PCI config DWORD 0x04 are W1C; the upper 16 bits of this
 * write must be zero, never a copied Status halfword. */
static inline void nas_x86_qemu_pci_command_write(uint8_t bus, uint8_t slot,
                                                   uint8_t function,
                                                   uint16_t command) {
    const uint32_t address = UINT32_C(0x80000000) | ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) | ((uint32_t)function << 8) | 4U;
    const uint16_t select_port = 0xcf8U, data_port = 0xcfcU;
    __asm__ volatile("outl %0,%1" :: "a"(address), "Nd"(select_port)
                     : "memory");
    __asm__ volatile("outl %0,%1" :: "a"((uint32_t)command), "Nd"(data_port)
                     : "memory");
}

#endif
