# M9.2 — AArch64 joined real boot-architecture development

This is a **third genuine ISA implementation** (x86-64 full userspace, RV64 M0
plus allocator-backed Sv39/Sv48/Sv57 PTEs, AArch64 M0 only) rather than an
unchecked stub named an architecture backend.

- New `arch/aarch64/cpu.c` implements EL1, enabled stage-1 MMU and TTBR1
  4-KiB granule checks using actual AArch64 CurrentEL/SCTLR_EL1/TCR_EL1
  registers; no x86 CR3, RISC-V SATP or generic bitfield assumptions.
- Architecture linker script and independent Clang/LLD cross-ELF linked
  under `make ARCH=aarch64 port-kernel`.
- The existing Limine M0 HHDM + physical-frame bitmap allocation and
  free smoke runs unmodified; this is our **shared boot invariant**.
- AArch64 QEMU diagnostics temporarily use `HLT #0xF000` semihosting,
  explicitly enabled only for the emulator. This is **not** a PL011 or
  real-device serial driver; normal console will need mapped, discovered
  MMIO (cf. NaOS's AArch64 PL011/SPCR flow).
- Independent GitHub CI QEMU boot with AArch64 UEFI+Limine is required to
  verify a real kernel marker after the actual MMU and frame smoke.

Explicitly unsupported: user ELF, syscall entry, context switching, kernel
TTY/PL011 MMIO, stage-2 VM, AArch64 user page table, longarch backend.
No magic null-returning feature stubs and no changes to existing x86 or RV
user/kernel regressions. This stage uses actual architectural differences
to guide the next common ABI cut rather than speculative pluggability.
