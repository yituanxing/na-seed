# M38 — authentic package-managed userspace development environment

Use the same strategy as NaOS: install an existing Linux distribution's native
GCC, binutils, musl, headers and startup objects into the rootfs **on the host**.
No NaSeed-specific user libc, no linking the freestanding kernel klibc into
userspace, and no guest networking or APK implementation are needed.

The build starts from `rootfs/alpine/packages.txt`, uses Alpine v3.23 signed
package repositories, and records the original package list. Three identical
rootfs/probe flows target amd64, riscv64 and arm64. The initial filesystem is
still the immutable CPIO plus writable /tmp overlay, with a dedicated 1 GiB
QEMU acceptance configuration for large toolchain archives.

The former 96-entry CPIO index is replaced with frame-backed stable slabs,
a path hash index, duplicate/path validation and unchanged read-only semantics.
A host test mounts >1200 entries and verifies readdir, symlinks and remount.

Native release gate: GCC must start from CPIO, execute its own target cc1, as
and ld helpers, use original musl stdio/stdlib headers, crt1.o and libc.a to
build a static program in guest /tmp and execute it with correct stdout.

This is a deliberately difficult real-program kernel acceptance test. It
does not imply the full Linux ABI is already implemented. Diagnose unsupported
general syscalls and VFS behavior from the first real failure. Do NOT delete
the M35/M36 handcrafted freestanding compatibility fixtures until the real
GCC/musl path and equivalent AES/zlib coverage have passed all three arches.
When safe, delete the fixtures and migrate the old acceptance instead of
weakening historical regression coverage.
