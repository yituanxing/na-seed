#include "../arch/arch.h"
#include "../drivers/serial.h"
#include "platform.h"
/* Semihosted QEMU ARM64 console is test-only. The host verifies the
 * real shared Linux exit path before terminating the guest. */
void nas_platform_debug_exit(unsigned code) {
    nas_print("[ARM64] userspace debug-exit status=");
    nas_dec(code);
    nas_print("\n");
    nas_arch_halt();
}
