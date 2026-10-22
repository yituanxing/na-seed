# M12 — Concurrent process lifecycle, bounded task capacity and init adoption

This is one complete cross-subsystem stage built on the now-green M11
anonymous VM ownership, not one-syscall-per-CI. NaOS' evolved process
registry, wait-state model and run queues were reviewed: their scheduler
supports preemption and separate kernel stacks, so NaSeed must not copy
their CFS/RT implementation into the current **cooperative single-stack**
kernel before it has the underlying mechanisms.

## System capability
- One shared `nas_task` registry now has **16** process slots (up from 4),
  on all three native ISA kernels. The common fork path returns `-EAGAIN`
  if all slots are occupied; slot reuse occurs **only** after wait4's
  address-space reclaim. PID identities monotonically increase, never
  recycle, and reject overflow of Linux 32-bit `pid_t`.
- When any non-PID1 process exits, existing live and zombie descendants
  are atomically adopted by PID 1. It owns future wait4/reaping. We
  deliberately do **not** silently auto-reap zombies: Linux requires
  init (or an explicit subreaper) to do so. Current BusyBox PID1 exits
  in the test scenario; a persistent init/reaper belongs to a later stage.
- Existing shared scheduler parks real pipe read/write/wait4 syscalls,
  restores each task's actual root/TLS/FPU and wakes only on readiness.
  Tests exercise **14 blocked children concurrently**, FIFO pipe wakeups,
  statuses, wait4/WNOHANG/ECHILD, 2 full waves of slot reuse, correct
  open-description references and a live orphan adopted by PID 1.
- No dishonest preemption, multi-threaded CLONE_VM, invented syscall
  completion, fake success or per-ISA process forks.

## Two-route acceptance
A single original C program `tests/m12-process-lifecycle.c` is built
static for x86-64/RV64/AArch64 with existing Linux libc/toolchains. It
first passes host Linux or QEMU-user oracle, then runs in the **same native
QEMU system/kernel** as source-controlled BusyBox. Under NaSeed it also
asserts the deliberate process-slot exhaustion EAGAIN. Both the parent
and all children genuinely execute the common syscall and VM paths.

The source-built workload uses a **generic directory** of native test
ELFs in the existing CPIO stage (not a hardcoded one-binary M11 special
case). The source BusyBox acceptance grows from **7 to 8 scenarios**,
while original, immutable Debian 17/17 and Alpine dynamic 7/7 remain
mandatory regression gates. The eight-case matrix shares the three
existing full-kernel QEMU test runners.

## Explicit boundary / next legitimate driver
This improves finite-process lifecycle and tests concurrent blocking,
**not** timer-driven preemption, true multi-core scheduling, or a
long-running PID1 userspace service. The latter must introduce a genuine
init that waits for adopted children before zombie reclamation can be
certified over indefinite uptime.

## First native integration evidence: no false pass on BusyBox ash tail-exec

Original three ISA full-QEMU tests reached the real m12 binary, while
BusyBox ash tail-`execve`d a sole final external command into its own
PID 1 slot. The 16-slot capacity test correctly rejected this: with
14 live children PID 1 leaves one extra slot, so EAGAIN is **not**
expected. This was a workload orchestration mismatch, not a defect
in process slot allocation or an excuse to weaken the assertion.

The shared source manifest now runs
`sh -c "/bin/m12-process --naseed-limit; echo process-parent-done"`
so the BusyBox shell **remains PID 1**, starts the real tester as PID 2
and leaves exactly 14 remaining slots. The tester independently checks
its PID 1 parent. All three ISA tests must be rerun after this correction.

## Second complete cross-ISA test finding: wait4 pid_t width at ABI boundary

After preserving BusyBox ash PID1, the real x86-64 glibc test
successfully occupied all 16 slots and got Linux EAGAIN on the extra
fork. The next real call `waitpid(-1, WNOHANG)` returned ECHILD
instead of 0 while **14 children were blocked alive on the same pipe**.

The x86-64 libc actually emits `or $0xffffffff,%edi` for signed
`pid_t=-1`, and the hardware zero-extends this into RDI as
`0x00000000ffffffff`. All three NaSeed layers previously interpreted
`-1` as a full 64-bit `UINT64_MAX`:

- process wait4 child filtering;
- blocked-wait scheduler wake eligibility;
- syscall dispatcher deciding whether a blocking wait is supported.

The **one architecture-independent fix** decodes the Linux signed
32-bit `pid_t` once in `task/wait_pid.h`, reused by all three.
Host regression tests both -1 encodings, exact positive PIDs and
invalid process-group forms. There are no x86-specific special cases
or edits to the original Linux user program. Native three-ISA CI
must be rerun after this correction.
