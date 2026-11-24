#ifndef NASEED_X86_PCI_CONFIG_H
#define NASEED_X86_PCI_CONFIG_H
#include <stdint.h>

/* CF8/CFC configuration mechanism #1: caller owns interrupt exclusion.
 * Segment 0, conventional config space only. Keep the original write
 * widths: legacy Q35 VirtIO uses a word Command write; the qualified H
 * profiles use a DWORD with a zero (never copied) W1C Status halfword. */
static inline uint32_t nas_x86_pci_address(uint8_t bus,uint8_t slot,
                                          uint8_t fun,unsigned offset) {
    return UINT32_C(0x80000000)|((uint32_t)bus<<16)|
           ((uint32_t)slot<<11)|((uint32_t)fun<<8)|(offset&0xfcU);
}
static inline void nas_x86_pci_select(uint8_t bus,uint8_t slot,
                                     uint8_t fun,unsigned offset) {
    uint32_t addr=nas_x86_pci_address(bus,slot,fun,offset);
    __asm__ volatile("outl %0,%1"::"a"(addr),"Nd"((uint16_t)0xcf8):"memory");
}
static inline uint32_t nas_x86_pci_read32(uint8_t bus,uint8_t slot,
                                         uint8_t fun,unsigned offset) {
    nas_x86_pci_select(bus,slot,fun,offset);
    uint32_t value;
    __asm__ volatile("inl %1,%0":"=a"(value):"Nd"((uint16_t)0xcfc):"memory");
    return value;
}
static inline void nas_x86_pci_write32(uint8_t bus,uint8_t slot,
                                      uint8_t fun,unsigned offset,uint32_t value) {
    nas_x86_pci_select(bus,slot,fun,offset);
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"((uint16_t)0xcfc):"memory");
}
static inline void nas_x86_pci_write_command16(uint8_t slot,uint8_t fun,
                                               uint16_t command) {
    nas_x86_pci_select(0,slot,fun,4U);
    __asm__ volatile("outw %0,%1"::"a"(command),"Nd"((uint16_t)0xcfc):"memory");
}
#endif
