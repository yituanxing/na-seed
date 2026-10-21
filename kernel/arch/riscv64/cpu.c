#include <stdint.h>
#include <stdbool.h>
#include "cpu.h"
/* Limine paging mode values 0/1/2 -> Sv39/Sv48/Sv57.
 * satp.MODE values 8/9/10. Refuse unknown or mismatched modes. */
uint64_t nas_riscv_satp_read(void) {
    uint64_t value;
    __asm__ volatile ("csrr %0, satp" : "=r"(value));
    return value;
}
unsigned nas_riscv_page_table_levels(uint64_t mode) {
    switch (mode) {
    case 0: return 3;
    case 1: return 4;
    case 2: return 5;
    default: return 0;
    }
}
bool nas_riscv_validate_limine_mode(uint64_t mode) {
    if (!nas_riscv_page_table_levels(mode)) return false;
    return (nas_riscv_satp_read() >> 60) == (mode + 8);
}
/* Strict hardware-derived user VA limit: Sv39, Sv48, Sv57 as negotiated
 * with Limine. Do not promise 48-bit user mappings on an Sv39 CPU. */
uint64_t nas_arch_user_limit(void) {
    uint64_t mode = nas_riscv_satp_read() >> 60;
    if (mode < 8 || mode > 10) return 0;
    return 1ULL << (11 + (unsigned)(mode - 5) * 9);
}
bool nas_arch_user_nx_supported(void) {
    uint64_t mode = nas_riscv_satp_read() >> 60;
    return mode == 8 || mode == 9 || mode == 10; /* leaf X is mandatory */
}
void nas_arch_halt(void) {
    for (;;) __asm__ volatile ("wfi");
}
