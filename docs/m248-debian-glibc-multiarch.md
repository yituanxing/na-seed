# M248: original Debian 13 glibc `dash` as native NaSeed PID1 on RV64 and ARM64

M248 extends **M247's existing x86-64 native-QEMU evidence** to two other
architectures. The independent GitHub Actions workflow is
`.github/workflows/m248-debian-dash-3isa.yml`, with a two-job matrix:
`riscv64` and `arm64`. Its name refers to the resulting **three-ISA** coverage
including M247 x86-64; it does not silently rerun or replace M247.

The direct program entry is `/usr/bin/dash`, an unmodified Debian 13 (trixie)
dynamic glibc ELF. The program receives `/etc/m248-case.sh` as its argument.
The script exercises original Debian `true`, `cat`, `wc`, and `printf`, including
command substitution, file redirection, a background pipeline and `wait`.
BusyBox is only a host-side, verified fixture used when constructing the CPIO;
it is **not** PID1 and must not substitute for any of those original programs.

## Trust and runtime boundaries

1. The host fetcher authenticates Debian's signed `InRelease` with the Debian
   archive keyring, then obtains original `.deb` files matching authenticated
   `Packages` SHA256 and exact architecture/version pins. Downloading a package
   or running it under Linux-user emulation is **host preparation only**.
2. Before constructing the CPIO, each original ELF must match ELF64, its
   architecture (`EM_RISCV` or `EM_AARCH64`), approved dynamic PIE type, actual
   `PT_INTERP`, and reviewed `DT_NEEDED` closure. The original program, glibc
   loader and `libc.so.6` bytes are copied unchanged. The CPIO supplies the
   requested loader pathname and standard library search symlinks, without
   changing any program interpreter field.
3. `scripts/m248-probe-debian-dash-pid1.sh riscv64` and the corresponding
   `arm64` invocation boot NaSeed in **real system QEMU**, using the existing
   per-ISA kernel build and native BusyBox parity runner with a custom CPIO.
   Passing a host `qemu-riscv64` or `qemu-aarch64` reference is insufficient.
4. A successful guest run must prove that `/usr/bin/dash` was the initial
   CPIO-selected process, that its genuine ELF interpreter loaded, that four
   separate coreutils were executed with their original dynamic loaders, that
   shell/script output matches the expected values, and that the native
   NaSeed userspace process exited successfully. A timeout, trap, `[FAIL]`,
   missing marker, or unreviewed dependency is a **failure**.

## CI execution and evidence

The workflow runs on `ubuntu-24.04`, uses `fail-fast: false`, and provisions
the real RISC-V or AArch64 system QEMU firmware in separate matrix jobs.
It restores the project's pinned official multi-ISA Limine seed, performs
local source/negative-contract checks, and calls exactly:

```sh
sh scripts/m248-probe-debian-dash-pid1.sh riscv64
sh scripts/m248-probe-debian-dash-pid1.sh arm64
```

Each matrix job executes **only its own** command. CI archives the signed
source/dash provenance manifests, workload manifest and native serial logs
for inspection. The native runners' `matrix-source` paths are:

- RV64: `build/port-riscv64/full/matrix-source/`
- ARM64: `build/port-aarch64/full/matrix-source/`

Expected proof includes the entry-selection marker
`[M5] CPIO executable selected: /usr/bin/dash`, the real dynamic interpreter
marker `[DYN] real ELF interpreter loaded, entry=`, original `execve` entries
for `true`, `cat`, `wc`, `printf`, the architecture-specific final output, and
`[PASS] cmdline userspace exit 0` **and** the matching native architecture
debug-exit marker. The architecture-specific final output is
`M248-DASH-PID1-RV64-OK` or `M248-DASH-PID1-ARM64-OK`, respectively.
These are claims to **verify from the archived guest serial logs**;
this document is not itself a PASS attestation.

## Scope and follow-up

M248 validates the original Debian `dash`/coreutils executable and glibc
loader ABI on three native NaSeed architectures once the RV64/ARM64 jobs pass.
It is **not** a claim that a complete Debian distribution boots under NaSeed.
Alpine/musl (M244), x86 glibc (M245/M246/M247), and their independent workflow
gates remain separate. The M248 gate does not introduce graphical desktop
dependencies or relax the unchanged-binary rule.
