#include "../kernel/arch/linux_syscall_abi.h"
_Static_assert(NAS_SC_read != NAS_SC_write, "distinct semantic operations");
/* Compile-time cross target ABI hook without glibc or userland dependencies. */
enum nas_syscall_op port_decode(uint64_t raw) {
    uint64_t call[7] = {raw, 0, 0, 0, 0, 0, 0};
    return nas_linux_decode_call(call);
}
