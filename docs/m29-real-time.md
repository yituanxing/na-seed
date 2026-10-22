# M29 — real monotonic time, deadline waits, three native ISAs

A complete shared semantic layer from three physical time counters through
Task deadline ownership to Linux userspace; no fabricated timeouts.

## Hardware providers
- x86-64: use real PIT channel-0 mode-3 periods to calibrate an actual TSC
  clock on the QEMU BSP before first user entry. TSC advances even while
  syscall C code masks IRQ0; unlike IRQ-tick counting, it can wake the last
  sleeping process. No assumed TSC MHz or RTC. PIT period is approximate;
  report conservative 1ms resolution.
- RV64: QEMU virt `time` CSR / pinned 10MHz timebase, same counter used by
  SBI TIME preemption, read without an IRQ or SBI timer submission.
- AArch64: CNTVCT_EL0 and actual CNTFRQ_EL0 in the existing Generic Timer,
  independent of whether the current Task receives PPI27.

## Linux ABI contract
- clock_gettime/clock_getres: CLOCK_MONOTONIC only. CLOCK_REALTIME reads
  still refuse with EOPNOTSUPP until a valid wall-time source exists.
- nanosleep (x86 native) and clock_nanosleep (all three); relative
  CLOCK_REALTIME sleep is valid even without an actual wall clock because
  elapsed duration is monotonic. Absolute sleep requires MONOTONIC.
- poll and ppoll finite waits use the SAME Task-owned monotonic deadline,
  waking on whichever happens first: actual readiness or expiry. No relative
  timeout restart on replay, and no dereferencing another Task's memory.
- ppoll with atomic temporary sigmask deliberately remains unsupported,
  pending userspace signal frames and rt_sigreturn.

## Scheduler and limitations
The syscall-boundary scheduler can idle-spin on real hardware time when all
Tasks are asleep. It intentionally does not enable hard IRQs in kernel C;
kernel preemption and interrupt-driven low-power idle are separate steps.
Wake-up is checked at scheduling boundaries and during user timer IRQs,
not by an invented background callback in kernel mode. Short finite RAM
waits have real monotonic expiry. No RTC, wall clock, alarm/setitimer or
userspace handler/rt_sigreturn is claimed.

## Cross-architecture gates
The identical `tests/m29-time.c` is built static for amd64, riscv64 and arm64
and first run on actual Linux (native or QEMU-user oracle). It then runs
under each NaSeed native QEMU full kernel from the immutable CPIO archive:
clock_gettime/getres, relative and absolute sleeps, empty-fd poll/ppoll,
pipe finite timeout vs readiness, and real elapsed monotonic durations.
The controlled-source BusyBox config now enables its *original* sleep
applet; the common matrix runs `sleep 1` on all three ISAs. Existing BusyBox
TTY, pipe, fork and timer-preemption parity must also stay green.

Do not merge without strict freestanding preflight and QEMU evidence.
