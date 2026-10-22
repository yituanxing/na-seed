#include "../arch/arch.h"
#include "../drivers/serial.h"
#include "platform.h"
/* RV QEMU virt has no ISA debug-exit I/O port. An actual syscall exit
 * success prints its precise kernel PASS marker before entering WFI.
 * The QEMU log monitor verifies this marker instead of a fabricated exit. */
void nas_platform_debug_exit(unsigned code) {
    nas_print("[RV64] userspace debug-exit status=");nas_dec(code);
    nas_print("\n");
    nas_arch_halt();
}
