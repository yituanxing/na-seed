#include "../kernel/arch/linux_stat_abi.h"
_Static_assert(offsetof(struct nas_linux_stat, ino) == 8, "inode");
int nas_stat_wire_length(void) { return (int)sizeof(struct nas_linux_stat); }
