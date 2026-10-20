# NaSeed M4.5 — engineering audit before M5

Date: 2026-10-20. Baseline: M0 + M1 + musl Bash + Debian glibc Bash/BusyBox +
Alpine dynamically linked musl BusyBox, all QEMU-accepted. This document
does **not** claim complete Linux ABI, robust isolation, or a general VFS.

## Implemented in this small checkpoint

- Stop the CI-reproducible ~30-second **unnecessary source rebuild** of
  cached static musl Bash: the build script is an *order-only* Make
  prerequisite; GitHub cache has a content-hashed builder key, and a
  cached ELF is independently checked for format, static linkage and
  real Linux-host command execution before use.
- Add a visible `make preflight` gate: changed-commit whitespace checks,
  working-tree/staged diffs, POSIX shell syntax, freestanding kernel
  compilation + link, and ELF header sanity. CI does this **before**
  QEMU tests. PR changes must be reviewed as a **batch** before upload.
- Centralize `ARCH_GET_FS` copy-to-user through the preexisting
  permission-aware helper. This avoids modifying read-only userspace
  pages through the kernel's writable physical HHDM alias.
- Tighten the leaf user pointer lookup to require both P_PRESENT and
  P_USER. Extend the independent M1 **real ring-3 ELF** regression:
  (1) `ARCH_GET_FS` targeting R-X text returns -EFAULT,
  (2) the same operation targeting user stack succeeds,
  (3) `write` cannot read kernel higher-half memory.

## Read-only audit findings for the next design discussion

1. **Kernel architecture:** `kernel/m1.c` is now large and mixes VM,
   ELF loading, syscall dispatch, simple FD/BootFS logic and test setup.
   Next real VFS work should introduce small interfaces; avoid a giant
   speculative split solely for aesthetic reasons.
2. **Filesystem:** `bootfs_lookup()` has fixed `/bin/busybox` and musl
   paths, and `build_linux_stack()` chooses test argv by filename.
   Replace these test-driven mappings with a real CPIO `newc` archive
   parser + read-only file/dir objects, *then* `getdents64`,
   `stat/openat/read` tests from unmodified BusyBox `ls` and `cat`.
   Do not claim this is already a VFS.
3. **VM/ELF:** Four-level, one-process address space, hardcoded ET_DYN
   load bias, no VMA metadata, incomplete access/execute protections and
   rollback behavior in `mmap/munmap/mprotect/brk`. Audit these before
   enabling writable VFS, `fork` or multitasking. Never return fake
   success for unsupported VM flags.
4. **Process/signals:** `rt_sigaction` stores handlers but **no delivery**
   exists, no scheduler/terminal/process tree. Treat these as explicit
   unsupported semantics in any new acceptance tests.
5. **Security scope:** ELF entry stack entropy is NOT cryptographic;
   Limine HHDM mappings and kernel global state persist; this kernel is
   experimental. No claim of isolation from malicious userspace.

## M5 acceptance contract — not yet implemented

Use a fixed real CPIO initramfs with at least `/bin/busybox`,
`/lib/ld-musl-x86_64.so.1` and `/etc/hostname`.
First aim for genuine dynamic `busybox ls /` and
`busybox cat /etc/hostname`; assert stdout/exit against an independent
known archive fixture. Discover/fix missing classes of syscalls, preserve
M0-M4 QEMU regressions, review changed code and compile locally
before scheduling full CI. Writable `mkdir`, `exec/fork/wait`, storage
and hardware drivers remain separate milestones.
