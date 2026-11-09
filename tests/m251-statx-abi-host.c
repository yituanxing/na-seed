/* Independent, compilable Linux UAPI oracle for the NaSeed statx wire ABI.
 * The fixed 256-byte statx representation is architecture-independent LP64.
 * Host run does not substitute for true native RV64/AArch64 NaSeed execution. */
#include <stddef.h>
#include <linux/stat.h>
#include "../kernel/arch/linux_stat_abi.h"

_Static_assert(sizeof(struct nas_linux_statx)==sizeof(struct statx),
               "NaSeed statx size differs from Linux UAPI");
_Static_assert(sizeof(struct nas_linux_statx_timestamp)==
               sizeof(struct statx_timestamp),"statx timestamp size");
_Static_assert(offsetof(struct nas_linux_statx, mask)==offsetof(struct statx, stx_mask),
               "statx mask offset");
_Static_assert(offsetof(struct nas_linux_statx, blksize)==
               offsetof(struct statx, stx_blksize),"statx block size offset");
_Static_assert(offsetof(struct nas_linux_statx, mode)==offsetof(struct statx, stx_mode),
               "statx mode offset");
_Static_assert(offsetof(struct nas_linux_statx, ino)==offsetof(struct statx, stx_ino),
               "statx inode offset");
_Static_assert(offsetof(struct nas_linux_statx, size)==offsetof(struct statx, stx_size),
               "statx size offset");
_Static_assert(offsetof(struct nas_linux_statx, atime)==offsetof(struct statx, stx_atime),
               "statx atime offset");
_Static_assert(offsetof(struct nas_linux_statx, btime)==offsetof(struct statx, stx_btime),
               "statx birth time offset");
_Static_assert(offsetof(struct nas_linux_statx, ctime)==offsetof(struct statx, stx_ctime),
               "statx ctime offset");
_Static_assert(offsetof(struct nas_linux_statx, mtime)==offsetof(struct statx, stx_mtime),
               "statx mtime offset");
_Static_assert(offsetof(struct nas_linux_statx, dev_major)==
               offsetof(struct statx, stx_dev_major),"statx device major offset");
_Static_assert(offsetof(struct nas_linux_statx, dev_minor)==
               offsetof(struct statx, stx_dev_minor),"statx device minor offset");
_Static_assert(offsetof(struct nas_linux_statx, mnt_id)==
               offsetof(struct statx, stx_mnt_id),"statx mount ID offset");

int main(void) { return 0; }
