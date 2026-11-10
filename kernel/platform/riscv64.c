#include "../arch/arch.h"
#include "../drivers/serial.h"
#include "platform.h"
/* RV QEMU virt has no ISA debug-exit I/O port. An actual syscall exit
 * success prints its precise kernel PASS marker before entering WFI.
 * The QEMU log monitor verifies this marker instead of a fabricated exit. */
void nas_platform_debug_exit(unsigned code) {
    nas_print("[RV64] userspace debug-exit status=");nas_dec(code);
    nas_print("\n");
#ifdef NAS_M241_PLATFORM_POWERDOWN
    /* M241 is a dedicated full-distribution shutdown image. Only after the
     * shared kernel has reaped PID1 and durably unmounted ext4 do we ask the
     * real machine firmware to power down. QEMU virt's M-mode OpenSBI owns
     * the SBI SRST extension (EID 'SRST', FID 0, type=shutdown, reason=0).
     * Successful firmware shutdown does not return. Other parity profiles
     * retain their historical serial-marker + WFI contract. */
    if(code==0) {
        register unsigned long a0 __asm__("a0")=0;
        register unsigned long a1 __asm__("a1")=0;
        register unsigned long a6 __asm__("a6")=0;
        register unsigned long a7 __asm__("a7")=0x53525354UL;
        __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
        nas_print("[RV64 SBI SRST FAIL] shutdown returned SBI error=");
        nas_hex(a0);
        nas_print("\n");
    }
#endif
    nas_arch_halt();
}

void nas_platform_restart(void) {
#ifdef NAS_M242_PLATFORM_REBOOT
    /* SBI SRST: type 1 is a cold reboot of the same machine instance. */
    register unsigned long a0 __asm__("a0")=1;
    register unsigned long a1 __asm__("a1")=0;
    register unsigned long a6 __asm__("a6")=0;
    register unsigned long a7 __asm__("a7")=0x53525354UL;
    __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
    nas_print("[RV64 SBI SRST FAIL] cold reboot returned SBI error=");
    nas_hex(a0);
    nas_print("\n");
#endif
    nas_arch_halt();
}
