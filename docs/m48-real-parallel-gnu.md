# M48 — real GNU Make/GCC parallel build plus GNU xargs on native NaSeed

Deliver one substantial authentic Linux userspace workload instead of more
freestanding syscall fixtures. The complete signed Alpine APK set includes
**GNU Make**, **GNU findutils xargs**, **GCC**, binutils, musl headers and
runtime. Packages are installed **on the host** for the target ISA with the
official verified minirootfs keyring. No guest-built packaging, fake syscall,
custom libc or one-off ABI emulation.

The guest's real GNU Make -j2 launches two independent shell recipes, each
rendezvous-polls for the other recipe before invoking the original GNU GCC,
as and ld to compile/link a genuine static musl ELF. This proves make's
actual concurrent scheduler rather than merely passing -j2. The compiled
executables produce independent markers and exercise native readv and
heap allocation. GNU xargs -P2 then launches two independent shell workers,
rendezvous-polls and executes both just-built ELFs. A second make invocation
checks existing target timestamps and dependency semantics.

The same workload must pass original x86-64/RV64/AArch64 full-kernel QEMU,
plus all existing M38–M47, ARM64, original TinyCC and userspace regressions.
Hard limit to two parallel jobs because NAS_TASK_LIMIT remains 16, no claim
of arbitrary POSIX SMP jobserver scalability. Real unsupported syscalls
or resource limits must produce genuine kernel fixes, not test-specific
hardcoding or weakened assertions.
