# M14 — Linux process groups, sessions and wait4 group selectors

NaOS's init installs its shell process group, creates a controlling
session and sets terminal foreground groups via separate userland
policy. NaSeed's previous `getpgid`/`getpgrp` unconditionally returned
the caller PID and wait4 supported only exact-PID/-1 selection. This
stage adds genuine **shared POSIX session/process-group mechanics**,
without claiming PTY, terminal foreground switching or signals.

- `nas_task` owns per-process `pgid`, `sid` and an `execed` marker:
  PID1 initializes the boot session/group, fork inherits them, successful
  exec retains IDs but prevents parent setpgid(child) after exec, and
  wait4 reclaim clears task records.
- New **common Task session module** handles setsid, setpgid, getsid,
  getpgid and getpgrp with Linux permission/error semantics (session
  leaders cannot move group, group joins require an existing same-session
  group, parents cannot move execed children, cross-session moves denied).
- x86 Linux UAPI and native generic64 RV/ARM syscall numbers are decoded
  to **the same internal ops**: no per-ISA task implementations.
- Linux signed32 wait4 pid_t selectors now include exact positive PID,
  -1 (any), 0 (current caller group) and -PGID (explicit group).
  The same predicate is shared by synchronous process wait and
  syscall-boundary blocked scheduler wake/replay.
- One untouched standard C source is cross-compiled with normal Linux
  libc to all three static native ELFs, verified under Linux/QEMU-user
  first, and then executed as true user code in each full NaSeed QEMU
  kernel. It checks group creation/inheritance, setsid and EPERM,
  getpgid/getsid, WNOHANG, positive/negative/group-zero waits, pipe
  wake/reaping and errno cleanup. Child readiness handshake removes
  scheduler races between ordinary Linux and NaSeed.
- Controlled-source workload expands 9 to 10. Immutable original
  Debian static 17/17 and Alpine/musl dynamic 7/7 remain the mandatory
  binary compatibility oracles.
- This is a **complete job-control foundation**, not a claim that
  tcsetpgrp, controlling TTY, terminal-generated signals, or
  timer-preemptive scheduling exist; those require another real
  terminal workload and a coherent TTY/signal stage.
