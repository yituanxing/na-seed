# M31 — Linux-style clocksource/timekeeper boundary (boot RTC)

## What is implemented

Read the same Linux bootloader RTC epoch on all three native ISAs through
Limine's `date_at_boot` response, then anchor it to the existing hardware
monotonic clocksource **after** the architecture has calibrated/initialized it.

- `kernel/time/clock.c` owns the validated wall-clock anchor and the
  conversion `CLOCK_REALTIME = epoch + (MONOTONIC - mono_anchor)`.
- `kernel/arch/{x86_64,riscv64,aarch64}/timer.c` remain unchanged as physical
  counter/interrupt providers (TSC/PIT, SBI TIME, CNTVCT/GIC).
- The shared syscall layer returns native LP64 `clock_gettime`,
  `clock_getres`, `gettimeofday` and absolute `clock_nanosleep`
  `CLOCK_REALTIME` semantics. Timed Task waits are still monotonic.
- Invalid/unavailable firmware RTC leaves realtime unsupported; monotonic
  reads/sleep/poll and user preemption continue normally.
- The existing `tests/m29-time.c` Linux/source BusyBox oracle now additionally
  verifies epoch, the two clocks' agreement and an absolute realtime wait.

This is **not** a full RTC subsystem or high-res timer queue. We deliberately
do not synthesize a date from TSC/time/CNTVCT, and do not pull a large NaOS
driver framework into a small kernel.

## Why this architecture

Linux distinguishes **clocksource** (read an advancing count) from
**clockevent** (program an interrupt), with a timekeeper connecting a stable
timeline to user-visible realtime. NaOS similarly separates generic RTC
device operations, hardware RTC drivers, deadline sources and timerfd.

NaSeed currently has real per-ISA interrupt providers and a Task-owned
deadline scheduler but no SMP, adjustable wall time, RTC class or dynamic
clockevent. This stage establishes a narrow common timekeeper interface
without duplicating architecture-specific code.

## Explicit limits / next work

1. The Limine timestamp is second-resolution and is sampled at firmware
   handoff, before timekeeper init. Late-init wall time can therefore have
   boot-calibration lag and up to one second epoch uncertainty. Clock
   *resolution* is the hardware monotonic granularity, not an assertion of
   absolute UTC accuracy.
2. No `clock_settime`, RTC driver probing, NTP, `adjtimex`,
   `getitimer`/`setitimer`, `timerfd`, time namespaces or suspend/resume.
3. Tick-based 100 Hz preemption and idle-spin waits remain: no one-shot
   programmable deadline device, no true kernel-C preemption or tickless idle.
4. Before making realtime adjustable, move realtime absolute deadlines to
   clock-domain-aware queues; changing UTC must not corrupt monotonic waits.
5. Keep exact unchanged native BusyBox/Linux-oracle QEMU runs required on
   x86-64/RV64/AArch64. No claiming success until every job finishes green.

Reference: Linux `Documentation/timers/timekeeping.rst`, `kernel/time/`,
NaOS `kernel/src/drivers/rtc.c`, `kernel/src/drivers/deadline.h` and
per-architecture timer/RTC drivers. NaOS code is a design reference only.
