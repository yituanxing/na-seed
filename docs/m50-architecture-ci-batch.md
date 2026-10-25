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

## Original M45 test race discovered by concurrent full-matrix CI

The original strict robust-futex test did `clone()` followed immediately by
`get_robust_list(worker_tid)`, assuming the worker had already run its
`set_robust_list` syscall. Under 12 concurrent native CI QEMU jobs,
RV64 correctly returned an **unregistered** per-TID list and the test
exited 59. That is a **userspace test ordering bug**, not grounds to
weaken the kernel's per-thread robust behavior. Fix once, across all
architectures: real shared-memory FUTEX_WAIT/WAKE handshake after the
worker registers and a second release barrier before it may exit. Keep
all previous exact list-offset, owner-death and clear_child_tid checks.

## Architectural scope and CI consolidation

The existing 12 original userland probes have **not been removed, weakened
or replaced**. Each still checks its own complete native QEMU serial output
and all its previous required PASS markers, signing and executable provenance.
New `scripts/native-alpine-matrix.tsv` is the *single ownership roster*:

- foundation: M38 real GCC/musl, M39 Lua/jq/SQLite, M40 tar/xz, M41 GNU pipes;
- pthread: M42 XZ pthread, M43 Zstd pthread, M44 exit_group;
- lifecycle: M45 Robust Futex, M46 execve in pthread group, M47 tkill/tgkill;
- parallel: M48 original GNU Make -j2/xargs -P2 and M49 Make -j4/xargs
  -P4 with 24 true live fork children.

Each of the 3 ISAs has one CI job per suite: **12 total QEMU installer jobs**
instead of 36, *36 unchanged actual native QEMU acceptance workloads*.
Each job reuses its tool installation and kernel build. First integrated run
proved 9/9 jobs succeeded but RV64's six sequential pthread/lifecycle tests
controlled the PR's critical path (~9.5 minutes). Splitting just that
slow suite into three-workload groups preserves all 36 tests and accelerates
wall-clock feedback at a modest increase from 9 to 12 reused-install jobs. All historical
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

## 2026-10-25: normal PR fast gate versus explicit full source-build gate

Previous M50 grouped foundation inadvertently included M55 CMake/Ninja's
40–55-minute *guest-side* build on **every** PR, while focused M56 and M57
also rebuilt BusyBox/SQLite on kernel/common-rootfs edits. This defeated
the normal userland prebuilt-binary workflow. Recent exact-head M57 M55
foundation stalled at guest GNU ld on all three ISAs, and M56 Kbuild stalled
or hit a runner shutdown. Neither result is evidence that original prebuilt
ELFs stopped running or that a particular kernel syscall is broken.

The immutable `native-alpine-matrix.tsv` still records **all 17** original
M38–M49/M51–M55 milestone probes. Normal PR CI runs the **12 source-free
prebuilt/ABI probes** on each of x86-64, RV64 and AArch64 (36 real guest
runs), reusing the original runner, serial assertions and build caches.
The original guest-source programs M38/M48/M49/M54/M55 are deferred only
in `NAS_NATIVE_MODE=fast`. The grouped `workflow_dispatch` input
`mode=full` (default for manual dispatch) executes **all 17** unchanged
probes on each ISA (51 actual system-QEMU workloads). M56 and M57 original
in-guest BusyBox/SQLite workflows similarly remain unchanged and explicitly
manually runnable, but no longer auto-trigger on ordinary PRs. M55 also
keeps its focused manual workflow. No program, stage marker, historic
source-build scenario or source-code assertion has been deleted.

This scheduling split **does not certify slow guest-source milestones** that
failed earlier: M55/Ninja full rebuild and M56 BusyBox Kbuild remain known
unresolved deep-suite failures until a complete native full run says otherwise.
For future heavy kernel/ABI changes choose and run full manually as a merge
gate; ordinary prebuilt-package changes get rapid cross-ISA ABI feedback
without 40-minute source builds. The original slow milestones are not
replaced with fake/prebuilt surrogate acceptance.
