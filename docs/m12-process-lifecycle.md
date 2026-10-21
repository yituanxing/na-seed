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
