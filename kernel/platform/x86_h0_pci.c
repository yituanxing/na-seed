#include "x86_h0_pci.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include <stddef.h>

/* x86 PCI configuration mechanism #1. Only CF8 receives a SELECT write;
 * CFC is read-only and no device PCI config DWORD is modified. The caller
 * owns interrupt exclusion so another CPU/interrupt cannot change CF8. */
static void config_select(uint32_t address) {
    __asm__ volatile("outl %0,%1" :: "a"(address), "Nd"((uint16_t)0xcf8)
                     : "memory");
}
static uint32_t config_data_read(void) {
    uint32_t value;
    __asm__ volatile("inl %1,%0" : "=a"(value)
                     : "Nd"((uint16_t)0xcfc) : "memory");
    return value;
}
static uint32_t pci_read32(void *context, uint8_t bus, uint8_t slot,
                           uint8_t function, uint16_t offset) {
    (void)context;
    /* Scanner requests standard aligned header DWORDs only. */
    uint32_t address = UINT32_C(0x80000000) | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)function << 8) |
                       (uint32_t)(offset & 0xfcU);
    config_select(address);
    return config_data_read();
}

static void print_hex_byte(uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    nas_putchar(hex[value >> 4]);
    nas_putchar(hex[value & 15U]);
}
static void print_hex_word(uint16_t value) {
    print_hex_byte((uint8_t)(value >> 8));
    print_hex_byte((uint8_t)value);
}

struct inventory_context {
    unsigned printed;
};
static int print_function(void *context, const struct nas_pci_function_info *d) {
    struct inventory_context *inventory = context;
    /* Bounded serial output: the scanner still counts all devices. */
    if (inventory->printed++ >= 64U) return 0;
    nas_print("[H0 PCI] ");
    print_hex_byte(d->bus);nas_putchar(':');
    print_hex_byte(d->slot);nas_putchar('.');
    nas_putchar((char)('0' + d->function));
    nas_print(" vendor=");print_hex_word(d->vendor_id);
    nas_print(" device=");print_hex_word(d->device_id);
    nas_print(" class=");print_hex_byte(d->class_code);
    print_hex_byte(d->subclass);print_hex_byte(d->programming_interface);
    if (d->header_type == 1U && d->class_code == 6U && d->subclass == 4U) {
        nas_print(" bridge=");print_hex_byte(d->secondary_bus);
        nas_putchar('-');print_hex_byte(d->subordinate_bus);
    }
    nas_print("\n");
    return 0;
}

int nas_x86_h0_pci_inventory(struct nas_pci_scan_stats *stats) {
    struct nas_pci_scan_stats local = {0};
    struct inventory_context inventory = {0};
    const struct nas_pci_config_reader reader = {NULL, pci_read32};
    /* Root bus 0 is a useful non-invasive compatibility probe, not a claim
     * that every host bridge/PCI segment was discovered. */
    int rc = nas_pci_scan_root(&reader, 0U, print_function, &inventory, &local);
    nas_print("[H0 PCI] scanned buses=");nas_dec(local.buses);
    nas_print(" functions=");nas_dec(local.functions);
    nas_print(" ignored-bridges=");nas_dec(local.bridges_ignored);
    nas_print("\n");
    if (local.functions > 64U)
        nas_print("[H0 PCI] serial details truncated after 64 functions\n");
    if (stats) *stats = local;
    return rc;
}
