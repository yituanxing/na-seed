#include "../arch/arch.h"
#include "../drivers/serial.h"
#include "platform.h"
/* Semihosted QEMU ARM64 console is test-only. The host verifies the
 * real shared Linux exit path before terminating the guest. */
void nas_platform_debug_exit(unsigned code) {
    nas_print("[ARM64] userspace debug-exit status=");
    nas_dec(code);
    nas_print("\n");
#ifdef NAS_M241_PLATFORM_POWERDOWN
    /* The tested QEMU 'virt' machine uses HVC as its PSCI conduit when
     * virtualization extensions are disabled. Use the standard PSCI 0.2
     * SYSTEM_OFF function (0x84000008), not a host signal or semihosted
     * process exit. The selected firmware/platform must actually implement
     * this conduit; a returning call is an explicit failure. */
    if(code==0) {
        register unsigned long x0 __asm__("x0")=0x84000008UL;
        __asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
        nas_print("[ARM64 PSCI FAIL] SYSTEM_OFF returned status=");
        nas_hex(x0);
        nas_print("\n");
    }
#endif
    nas_arch_halt();
}


void nas_platform_restart(void) {
#ifdef NAS_M242_PLATFORM_REBOOT
    /* PSCI SYSTEM_RESET over the tested QEMU virt HVC conduit. */
    register unsigned long x0 __asm__("x0")=0x84000009UL;
    __asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
    nas_print("[ARM64 PSCI FAIL] SYSTEM_RESET returned status=");
    nas_hex(x0);
    nas_print("\n");
#endif
    nas_arch_halt();
}
