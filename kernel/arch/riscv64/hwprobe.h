#ifndef NASEED_RISCV64_HWPROBE_H
#define NASEED_RISCV64_HWPROBE_H
#include <stdint.h>
#include "../../errno.h"
#include "../../mm/usercopy.h"

/* Linux RISC-V UAPI, arch/riscv/include/uapi/asm/hwprobe.h.
 * The current single-CPU NaSeed RV64GC image preserves user F/D state.
 * Do not advertise V, bitmanip, cache operations, or unverified extensions.
 * CPU-mask/WHICH_CPUS queries are not yet implemented; report an explicit
 * error rather than returning a fabricated CPU selection. */
struct nas_riscv_hwprobe_pair {
    int64_t key;
    uint64_t value;
};
_Static_assert(sizeof(struct nas_riscv_hwprobe_pair)==16,
               "Linux riscv_hwprobe pair ABI");

static inline uint64_t nas_riscv_hwprobe(uint64_t user_pairs,
                                        uint64_t pair_count,
                                        uint64_t cpusetsize,
                                        uint64_t user_cpus,
                                        uint64_t flags) {
    if (flags & ~1ULL) return (uint64_t)-(int64_t)EINVAL;
    if (flags || cpusetsize || user_cpus)
        return (uint64_t)-(int64_t)EOPNOTSUPP;
    if (pair_count > 64U) return (uint64_t)-(int64_t)E2BIG;
    if (!pair_count) return 0;
    if (!user_pairs || user_pairs > UINT64_MAX - pair_count * 16U)
        return (uint64_t)-(int64_t)EFAULT;

    struct nas_riscv_hwprobe_pair pairs[64];
    const uint64_t bytes = pair_count * sizeof(pairs[0]);
    if (!nas_user_read_bytes(user_pairs,(uint8_t *)pairs,bytes) ||
        !nas_user_prepare_write(user_pairs,bytes))
        return (uint64_t)-(int64_t)EFAULT;
    for (uint64_t i=0; i<pair_count; i++) {
        switch (pairs[i].key) {
        case 5: /* CPUPERF_0: misaligned performance unknown */
            pairs[i].value=0;
            break;
        case 3: /* BASE_BEHAVIOR: rv64ima */
            pairs[i].value=1U;
            break;
        case 4: /* IMA_EXT_0: F/D + C; V and other extensions absent */
            pairs[i].value=3U;
            break;
        default: /* Linux requires unknown keys to become (-1,0). */
            pairs[i].key=-1;
            pairs[i].value=0;
            break;
        }
    }
    return nas_user_write_bytes(user_pairs,(const uint8_t *)pairs,bytes)
        ? 0 : (uint64_t)-(int64_t)EFAULT;
}
#endif
