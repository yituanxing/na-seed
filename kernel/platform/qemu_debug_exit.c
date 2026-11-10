#include "platform.h"
#include "../arch/arch.h"
/* QEMU PC isa-debug-exit is a test device, not a CPU ISA feature. */
void nas_platform_debug_exit(unsigned code) {
    nas_arch_outb(0xf4, (unsigned char)code);
}


void nas_platform_restart(void) {
#ifdef NAS_M242_PLATFORM_REBOOT
    /* Q35 ACPI I/O reset register, following the x86 firmware reset
     * sequence: enable hard reset, then pulse reset. */
    nas_arch_outb(0xcf9, 0x02);
    nas_arch_outb(0xcf9, 0x06);
#endif
    nas_arch_halt();
}
