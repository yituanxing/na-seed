#include "../kernel/arch/linux_syscall_abi.h"
_Static_assert(NAS_SC_read != NAS_SC_write, "distinct semantic operations");
/* Compile-time cross target ABI hook without glibc or userland dependencies. */
enum nas_syscall_op port_decode(uint64_t raw) {
    return nas_linux_decode_syscall(raw);
}
