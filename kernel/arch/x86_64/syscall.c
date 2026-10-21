#include <stdint.h>
#include "../arch.h"
#include "../../task.h"
#include "../../arch/mmu.h"
#include "../../mm/usercopy.h"
#include "../../errno.h"
#include "syscall.h"
uint64_t nas_arch_prctl(uint64_t op, uint64_t address) {
    if (op == 0x1002) { /* ARCH_SET_FS */
        if (address >= USER_LIMIT) return (uint64_t)-EINVAL;
        nas_current_task->arch.user_fs_base = address;
        nas_arch_set_user_tls(address);
        return 0;
    }
    if (op == 0x1003) { /* ARCH_GET_FS */
        if (address >= USER_LIMIT || address > USER_LIMIT - sizeof(uint64_t))
            return (uint64_t)-EFAULT;
        uint8_t bytes[sizeof(uint64_t)];
        for (unsigned i = 0; i < sizeof(bytes); i++)
            bytes[i] = (uint8_t)(nas_current_task->arch.user_fs_base >> (8 * i));
        return nas_user_write_bytes(address, bytes, sizeof(bytes))
            ? 0 : (uint64_t)-EFAULT;
    }
    return (uint64_t)-EINVAL;
}
