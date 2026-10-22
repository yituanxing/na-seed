# M28 (first complete provider): x86-64 periodic user-mode preemption

Status: **x86-64 provider candidate**, not yet three-ISA M28.

This follows M27's one-16KiB-kernel-entry-stack-per-Task design.
The first provider uses a QEMU q35-compatible **i8259 PIC + 8254 PIT**
at roughly 100Hz. IRQ0 is remapped to vector 0x20. IRQs are only
schedulable when interrupted from CPL3; a kernel-mode IRQ fails closed
instead of using a user CPU snapshot.

## Important change: SYSRET to IRET

A timer can interrupt between *any* instructions, with arbitrary RCX/R11.
`SYSRET` always clobbers RCX and R11, so it cannot resume this CPU
snapshot. The new x86 `syscall_entry` and timer IRQ both return via IRET
using the same 15-GPR + five-quadword hardware return frame. The VM/TLS/FPU
state and user RIP/RFLAGS/RSP are stored separately in Task. No fake
"syscall replay" is used for ordinary CPU-preempted Tasks.

The shared scheduler has `nas_sched_preempt_select()`. It:
1. Prefers another READY Task in round-robin order.
2. Checks truly wakeable BLOCKED Tasks with the *existing* readiness and
   blocked-syscall replay code, in the owner's restored address space.
3. Revalidates EAGAIN/pipe-not-ready without returning fabricated success.
4. Saves/restores FPU and task control state via the existing providers.
5. Leaves the interrupted Task unchanged if no alternative is runnable.

No preemptive kernel C calls, kernel threads, APIC SMP, or portable timer
are claimed. A real kernel continuation mechanism will still be needed
before in-kernel blocking can suspend an arbitrary C function.

## Dual reference tests

`tests/m28-preempt.sh` is a **single identical POSIX shell script**.
Native Linux runs it through the original upstream BusyBox 1.37.0
`ash`; NaSeed runs precisely the same source script through the same
pinned BusyBox binary and Kconfig. A child writes a real tmpfs file and
then spins forever in the `:` builtin; the parent must get CPU time
to kill it and print `cpu-hog-did-not-starve-ash`. The guest harness
also demands actual `[M28] timer-preempt` telemetry from the native
hardware IRQ handler, not just a successful syscall sequence.

The script runs independently from 16/16 original source BusyBox cases,
all Debian/Alpine profiles, and prior TTY/signal tests. The failure mode
is a bounded QEMU timeout, not a CI job that hangs indefinitely.

Fast preflight: `sh scripts/check-m28-timer.sh`.
Full required acceptance: both CI workflows, original x86 CPU-hog
case, plus all original cross-ISA regressions. On RV64/AArch64, the
existing cooperative scheduling behavior is intentionally unchanged
until corresponding proper timer IRQ handlers are built and tested.
