# Native executable build and execution (NaSeed)

Goal: use the OS rather than implement every Linux syscall first.
Original GNU Make 4.4.1 reads a real Makefile, invokes original TinyCC
0.9.27, and writes a **new executable ELF** into volatile /tmp.
The original BusyBox shell uses chmod(2) to set execute permission,
then calls execve(2) on that tmpfs inode. The ELF bytes are not in
the original CPIO archive. Its raw Linux write(2) output proves the
native-generated program reached user mode.

Kernel architecture:
- Path decoding and dirfd handling stay in fs/syscall.c.
- fs/lookup.c selects the existing bootfs/tmpfs inodes without copying
  their contents, now shared by exec and access permissions.
- task/exec.c snapshots caller strings, stages a *fresh* address space
  and calls the existing multiarch ELF loader. No new loader, no
  architecture-specific special case and no host-supplied executable.
- tmpfs.c owns inode chmod permissions and storage; bootfs stays EROFS.
- Linux numeric syscalls are decoded at the existing arch ABI boundary.

This first milestone only needs a small freestanding TinyCC-linked ELF.
The tmpfs inode cap is explicitly bounded at 64 KiB, not claimed to be
a general file system or a place to link entire glibc binaries. Later,
larger real programs should drive a page-backed file cache, rather than
raising fixed arrays indefinitely.

Regression: native Linux *same upstream TinyCC* must link and execute
tests/native-hello.c. Real NaSeed QEMU must then print exactly
naseed-native-tcc-elf-ran and exit 0 after GNU Make, TinyCC,
BusyBox chmod and execve. Existing original GNU Make two-recipe
acceptance remains independent.

Architecture checkpoints: the shared lookup C unit is in the same
common full-kernel source list used by RV64 and AArch64. All platform
differences remain behind the existing arch providers.
