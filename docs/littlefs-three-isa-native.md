# Original LittleFS as a second real native three-ISA VFS implementation

On top of the unchanged pinned littlefs v2.11.2 and original BlockDevice,
one freestanding source `user/littlefs-native-vfs.c` is built as a genuine
unprivileged Linux ELF for x86-64, RV64 and AArch64. It invokes Linux
openat/read/close/getdents64/write/exit, validates the same file's data
and kernel-generated directory names, and exits zero.

Reuse `user/<isa>/ext4-rw-syscall.S` wrappers; the dispatch table is
extended by only the Linux getdents64 syscall number with existing indices
unchanged. A second, native QEMU x86 run selects this portable ELF; RV/ARM
QEMU launchers opt in to the same original littlefs module and real ELF.
All tests require the native kernel's own VFS mount marker, the userspace
pass marker, clean final unmount and BlockDevice detach, and native debug
exit. Default/other ext4 and physical VirtIO tests are unmodified.

This is read-only VFS over a borrowed volatile RAM image. It is not evidence
of physical write persistence, crash recovery, or POSIX writable unlink-open.
