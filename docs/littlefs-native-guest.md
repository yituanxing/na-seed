# 2026-10-28 — independently booted littlefs through native NaSeed VFS

Opt-in `NAS_ENABLE_LITTLEFS=1` builds **original unchanged BSD-3-Clause
littlefs v2.11.2** along with its shared BlockDevice bridge and VFS provider
for all native ISA targets. The kernel boot policy looks only for a
**specifically named** `naseed-littlefs.img` Limine RAM module, attaches
a borrowed RAM BlockDevice, and mounts its read-only original littlefs
filesystem at `/flash`. It refuses ambiguous duplicate images and does
not pretend RAM writes survive reboot or that the VFS provider is writable.

`scripts/make-littlefs-image.c` produces a real original littlefs volume.
`user/littlefs-guest-read.S` is a separate **unprivileged x86 Linux ELF**
using actual openat/read/getdents64/close/exit syscalls. The dedicated
`scripts/probe-littlefs-guest-x86.sh` boots QEMU OVMF/Limine and checks the
read, directory enumeration, successful process exit, and final VFS
unmount/BlockDevice release before allowing the CI gate to pass.

This verifies a second, independent VFS backend across **real native kernel
and syscall boundaries** (x86 QEMU runtime; RV64/AArch64 kernel object
link), not just host-only unit tests. It is still not a writable POSIX
littlefs or a durable physical disk; other architectures require dedicated
native guest runtime verification.
