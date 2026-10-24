# M50 — architectural source parity, workload-grain regression CI

## Audit, before modification

- Full userspace regression is seven parallel jobs; ARM64 full-kernel and
  TinyCC/Lua etc are separately focused.
- M38–M49 were **12 different automatic PR workflows** that installed the
  same QEMU/UEFI/LLVM tools separately for each native ISA. Each selected
  a single original Alpine userland profile. This inflated **36 installer
  jobs**, independent of whether the same kernel was already built.
- Earlier shared kernel manifest locked RV64 and AArch64 to the same
  29 non-architecture C translation units, but did **not verify the x86
  Makefile actually compiled and linked all 29**. The new fail-closed host
  cross-ISA contract closes that gap.
- One more full-kernel source copy or new per-ISA syscall implementation
  is **not** needed. Keep ISA-specific CPU/MMU/context/IRQ providers below
  shared Task, scheduler, process, signal, VFS and ELF source policy.

## Architectural scope and CI consolidation

The existing 12 original userland probes have **not been removed, weakened
or replaced**. Each still checks its own complete native QEMU serial output
and all its previous required PASS markers, signing and executable provenance.
New `scripts/native-alpine-matrix.tsv` is the *single ownership roster*:

- foundation: M38 real GCC/musl, M39 Lua/jq/SQLite, M40 tar/xz, M41 GNU pipes;
- threads: M42 XZ pthread, M43 Zstd pthread, M44 exit_group,
  M45 Robust Futex, M46 execve in pthread group, M47 tkill/tgkill;
- parallel: M48 original GNU Make -j2/xargs -P2 and M49 Make -j4/xargs
  -P4 with 24 true live fork children.

Each of the 3 ISAs has one CI job per suite: 9 total QEMU installer jobs
instead of 36, *36 unchanged actual native QEMU acceptance workloads*.
Each job reuses its tool installation and kernel build. All historical
12 focused workflows retain explicit `workflow_dispatch` for diagnostics
but no longer auto-trigger PR-wide. The new matrix triggers on any shared
kernel, program fixture, runner, base-image or CI change so M38–M41 now
also receive real PR kernel-change validation rather than incidental runs.

The check script rejects missing/duplicated suite entries or programs,
missing ISA routes, silently missing focused workflow, and any missing
x86 linker TU before the expensive native QEMU matrix starts.

One meaningful batch is now the unit of change: audit the full shared
dataflow; edit coherent common-layer owner code and acceptance together;
run static checks; one exact-head grouped native regression; merge only
after the new matrix and unchanged full userspace/ARM64 gates all pass.

Limits: does not consolidate or remove independent source BusyBox,
ARM64 hardware/TTY gates or core target-ISA QEMU tests. Nor does it claim
the kernel has SMP, arbitrary process count or complete Linux ABI.
