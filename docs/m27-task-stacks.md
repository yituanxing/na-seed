# M27 — Task-owned kernel entry stacks and real BusyBox acceptance

## Why this is necessary, and what it does NOT claim

The old syscall-boundary cooperative scheduler already switches independent
user CR3/TTBR0/SATP contexts, GPRs, TLS and FPU state. It does **not** preserve
arbitrary C frames on the one shared kernel stack, and cannot interrupt a
userspace CPU-bound loop that avoids syscalls.

This phase gives each of the 16 Task slots its own **16 KiB entry stack**,
including PID 1; a fork must never inherit its parent's stack pointer. We
retain the existing syscall-boundary blocking/replay semantics. Before a
different user task is resumed, each arch switches its next user→kernel
entry stack and x86 updates TSS.rsp0 as well. RV64/AArch64 relocate the
*completed* trap frame at the final return boundary so the fresh stack is
active when the task next traps. No live C continuation is copied.

Do **not** mistake this for full preemption or kernel threads:
- There is no saved kernel-stack pointer representing a suspended C call.
- No user-mode timer interrupt enters the scheduler yet.
- No mid-kernel preemption, kernel-safe locking or SMP.
- Stack-bottom **sentinels** detect some overflows, not guard-page faults.
- A kernel timer IRQ must use an independent audited trap and IRQ stack
  path, preserve all GPR/FPU/PC/flags, and schedule only at safe boundaries.

## Three levels of evidence (strictly separated)

1. **Host ownership & cross-ISA build**: `sh scripts/check-task-stacks.sh`
   compiles the actual `task.c` for x86/RV64/ARM64 and each arch's context
   C and trap assembly; native tests cover slot independence, fork, reuse,
   canaries and bogus pointers.
2. **Unmodified upstream BusyBox on native Linux oracle**:
   `sh scripts/check-kstack-busybox-oracle.sh build/source-busybox/amd64/busybox`.
   Same pinned source/config that NaSeed already uses. This exercises
   six process forks and six original `ash | wc` pipelines.
3. **Full NaSeed QEMU guest**: the TWO new cases `kstack-fork-stress`
   and `kstack-pipe-stress` in `source-busybox-cases.tsv` run the
   equivalent **actual upstream BusyBox** from CPIO on all 3 ISAs
   alongside the previous 14 workloads (now 16/16).
   Existing pinned Debian BusyBox and dynamic Alpine profiles remain.

## The next acceptance gate: real timer-driven preemption

`python3 scripts/busybox-preempt-oracle.py /usr/bin/busybox` exercises
an entirely standard stock BusyBox `ash` script on Linux:

`(echo hog-started; while :; do :; done) | { read started; echo cpu-hog-did-not-starve-ash; }`

The producer writes one readiness line and then runs a **pure userspace
CPU loop**. The consumer can print the witness only after reading it.
The pipe handshake ensures that a cooperative scheduler cannot accidentally
pass before the hog starts: a timer must preempt the spinning producer. The oracle launches a disposable process group and
kills all descendants. Until timer IRQs are implemented and the same
scenario completes under **real NaSeed QEMU**, do not label this acceptance
as passed. No kernel special-case for the script is permitted.

Other useful real-program stressors after this gate: the unmodified
BusyBox `ash`, `wc`, `cat` and `sha256sum` from pinned distro fixtures,
plus the existing Debian/Alpine and native termios regressions.

## Next design stage

Separate Task/Process metadata from a thread's saved kernel and user
contexts. Introduce a portable scheduling mechanism with architecture
hooks for x86 APIC, RISC-V S-mode timer and ARM Generic Timer. On a
timer interrupt from EL0/Ring3/U-mode, save exact user trap state, switch
the runnable task, and restore an independently validated return frame.
For interrupts in kernel mode, defer scheduling until a safe point (or
save a complete kernel continuation). Only then enable voluntary blocking
inside kernel C paths, SA_RESTART handling, and eventually SMP.
