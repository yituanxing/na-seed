# NaSeed

NaSeed is an independently developed operating-system kernel targeting x86-64, RISC-V 64 and AArch64. The kernel, architecture providers, userspace samples, filesystem adapters and vendored upstream sources remain in their existing directories.

## Rebuilt development entry points (2026-11-25)

This branch intentionally removes the accumulated milestone-specific acceptance framework, CI workflows, historical reports, distribution fixture generators and per-profile Limine configurations. **This is a tooling reset, not proof that old distribution or hardware integrations still pass.** Historical tests remain recoverable from Git history; do not treat their removal as feature validation.

Required tools: `clang`, `ld.lld`, `make`, `curl`, `readelf`; x86 UEFI ISO/boot additionally need `xorriso`, Limine downloads, `qemu-system-x86_64`, `ovmf`, and `timeout`. Core third-party library sources are tracked in this repository.

- `make ARCH=x86_64 kernel`: build full x86 kernel ELF.
- `make iso`: build x86_64 UEFI ISO using `limine.conf`.
- `make smoke`: QEMU boot and fundamental serial allocator marker check.
- `make run`: run that ISO in QEMU/OVMF.
- `make ARCH=riscv64 kernel` or `make ARCH=aarch64 kernel`: build each ISA's shared full kernel and native sample user ELF.
- `make clean`: delete generated build output.

The source list for RV64 and AArch64 is `scripts/full-kernel-core.sources`. A few **build-critical** filesystem and native compiler scripts are retained, not confused with obsolete acceptance tooling. The new CI runs build checks for all three ISAs and one x86 UEFI smoke boot. Advanced feature switches and original distribution/hardware acceptance require fresh coverage before being considered verified.

See `docs/architecture.md` for source ownership and the scope of this reset.
