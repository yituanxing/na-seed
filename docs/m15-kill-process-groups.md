# M15 — native Linux SIGKILL of real blocked child processes / groups

## Real application driver
M14 established portable session/PGID identity but cannot terminate a
process blocked on `pipe.read`, cannot signal a whole job group, and can't
report WIFSIGNALED(SIGKILL) to the parent. This milestone uses unchanged
standard Linux C ELF source compiled for amd64, riscv64 and arm64, verified
against host Linux and QEMU-user first, then inside the **actual** shared
NaSeed QEMU kernel. This is the prerequisite for terminal job control,
not a claim that PTY, termios, foreground TTY groups or SIGINT exist.

## Kernel mechanism
- ISA number x86 `__NR_kill`, generic64 `129`, to common NAS_SC_kill.
- Linux signed32 selectors: positive PID, 0 caller PGID, -1 all other
  eligible processes, negative PGID. Existence-checking signal 0.
- Real, non-maskable SIGKILL terminates READY or BLOCKED task(s); the
  common scheduler/task-exit path owns zombie, PID1 adoption,
  user-space child_tid clear and final FD/pipe close. The target VM
  must be active during this cleanup and caller MMU/TLS/FPU restored.
- One Linux wait4 invariant: WIFSIGNALED and WTERMSIG(SIGKILL) are
  observed by the real userspace parent after waitpid.
- Protect PID 1. Explicitly reject unsupported signals with
  EOPNOTSUPP; avoid pretending SIGTERM/INT/STOP or user-handler frames
  were delivered. A later full signal subsystem must replace this
  minimal SIGKILL transport.

## Acceptance
- Native C `tests/m15-kill.c`: invalid/signum0/missing PID, blocked
  child SIGKILL, two-member process group SIGKILL, negative PGID,
  signal-zero process probe, self-SIGKILL, wait/reap.
- `build-m15-kill.sh` checks the unchanged program under each Linux
  toolchain / QEMU-user before full kernel tests.
- Three unchanged Debian/Alpine distro binaries continue mandatory
  regression. Controlled-source profile adds a case (11 total), not a
  special-purpose kernel shortcut. CI must pass all real QEMU boots
  before merging.
