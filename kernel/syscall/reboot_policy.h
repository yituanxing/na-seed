#ifndef NASEED_SYSCALL_REBOOT_POLICY_H
#define NASEED_SYSCALL_REBOOT_POLICY_H
#include <stdint.h>
#include "../errno.h"

/* Linux reboot(2) UAPI, identical on x86-64/RV64/AArch64. The syscall
 * carries two magic words; the libc reboot() wrapper supplies both. */
#define NAS_LINUX_REBOOT_MAGIC1 UINT32_C(0xfee1dead)
#define NAS_LINUX_REBOOT_MAGIC2 UINT32_C(0x28121969)
#define NAS_LINUX_REBOOT_MAGIC2A UINT32_C(0x05121996)
#define NAS_LINUX_REBOOT_MAGIC2B UINT32_C(0x16041998)
#define NAS_LINUX_REBOOT_RESTART UINT32_C(0x01234567)
#define NAS_LINUX_REBOOT_HALT UINT32_C(0xcdef0123)
#define NAS_LINUX_REBOOT_POWER_OFF UINT32_C(0x4321fedc)

static inline uint64_t nas_linux_reboot_validate(uint64_t magic1,
                                                  uint64_t magic2,
                                                  uint64_t raw_command,
                                                  uint32_t *command) {
    if ((uint32_t)magic1!=NAS_LINUX_REBOOT_MAGIC1 ||
        ((uint32_t)magic2!=NAS_LINUX_REBOOT_MAGIC2 &&
         (uint32_t)magic2!=NAS_LINUX_REBOOT_MAGIC2A &&
         (uint32_t)magic2!=NAS_LINUX_REBOOT_MAGIC2B))
        return (uint64_t)-EINVAL;
    const uint32_t cmd=(uint32_t)raw_command;
    if (cmd!=NAS_LINUX_REBOOT_RESTART && cmd!=NAS_LINUX_REBOOT_HALT &&
        cmd!=NAS_LINUX_REBOOT_POWER_OFF)
        return (uint64_t)-EOPNOTSUPP;
    if (command)*command=cmd;
    return 0;
}
#endif
