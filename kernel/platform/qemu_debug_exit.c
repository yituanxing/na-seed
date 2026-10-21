#include "platform.h"
#include "../arch/arch.h"
/* QEMU PC isa-debug-exit is a test device, not a CPU ISA feature. */
void nas_platform_debug_exit(unsigned code) {
    nas_arch_outb(0xf4, (unsigned char)code);
}
