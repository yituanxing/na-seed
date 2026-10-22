# NaOS VFS alignment — first practical file-ops boundary

Reviewed NaOS `kernel/src/fs/vfs/vfs.h`, `vfs_file.c`, `vfs_lookup.c`,
and `kernel/src/task/task_struct.h`. NaOS owns separate fd entries,
open-file descriptions (`f_op`, f_pos, flags, refcount), inode, dentry and
mount objects. The callback design is more important than copying its
SMP lock/dcache/pagecache hierarchy into this limited single-CPU kernel.

This patch binds file callbacks once to the shared open-description slot
from real UART bootstrap, open/openat, or pipe2. read/write/poll/ioctl
dispatch directly through these operations; poll subscriptions use the same
object. Last-close uses its own release callback instead of trying every
backend. No Linux-visible file semantics should change. No additional
fake syscall success paths were added.

**Reviewed further, not yet abstracted:** open_path still hardcodes /dev,
read-only CPIO and /tmp; pathwalk is not a mount-aware component resolver;
TmpFS stores pathname in the inode; no inode-vs-dentry identity and no
general mount tree. fstat/lseek/getdents64/chdir/execve still access concrete
backends; struct nas_fd_table has parallel arrays and some backend pointers
per descriptor. These need a typed inode + shared file object, and directory
operations that preserve unlink/recreate and rename/fork semantics. Do not
introduce fake abstract objects merely to conceal path prefixes.

Limits remain: bounded read-only archive, bounded volatile RAM /tmp,
no block device or persisted data, 16 tasks and 64 FD slots. Keep all
three-ISA real BusyBox regressions, including pipe, TTY, poll and timer.
