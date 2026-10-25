# M67 — original procps-ng pgrep/pkill, live Task and signal validation

A smaller native Linux userspace milestone than strace/ptrace. We require
original signed Alpine 3.23 procps-ng `pgrep` and `pkill` on x86-64,
RV64 and AArch64 QEMU, without compiling inside the guest.

Two genuine BusyBox sleep processes retain distinct argv and PIDs.
Original `pgrep -f` must return their real PIDs (not the PID of pgrep
itself); `pgrep -x busybox` must match their kernel task comm.
Original `pkill -STOP` and `-CONT` must change actual Task state and
`/proc/PID/status`. Then `pkill -TERM` and `-KILL` target individual
processes, **without killing the surviving peer**. POSIX shell wait
requires 128+SIGTERM (143) and 128+SIGKILL (137) respectively. Finally
`pgrep` must report reaped processes absent.

Continue to use the existing Task, signal and procfs implementation on
all three ISAs; any kernel repair must land in those shared owners, not
per-program or per-ISA shortcuts. Validate exact-head native QEMU,
and preserve existing grouped 9/9 and other standard regressions.
Do **not** label ptrace or strace supported: those additionally need
tracer ownership, syscall enter/exit stops, register/memory access and
waitpid event semantics, plus per-ISA register ABI work.

The M67 fixture launches actual foreground sleepers on two M60-verified genuine
`tmux` PTY sessions; no mocked PID or fixed sleep process is used. Earlier
first-probe shell `sleep &` found a **separate BusyBox ash background redirection
failure** (`can't open /dev/null` in post-fork shell) on all three ISAs, despite
normal direct `/dev/null` redirection passing. That is tracked independently:
M67 does not claim all `ash &` semantics are fully compatible.

## M68 root-cause repair — Linux lowest-free FD contract

Original native BusyBox ash `command &` failed opening `/dev/null` while
foreground redirected reads were fine. **Root cause:** the M62 FD object
adapters for `open/openat`, `pipe/pipe2/socketpair`, `socket`, and
`accept/accept4` explicitly searched from descriptor 3, even when fd0/1/2
were closed. Linux allocates the **lowest available descriptor from 0**;
the shell background child closes stdin and expects its subsequent
`open("/dev/null", O_RDONLY)` to become **fd0**. It instead became fd3,
producing the shell's `No error information` message (errno remained 0
because the syscall had succeeded).

Correct the existing shared ownership layer callers, not the shell or a
per-program syscall branch. `fcntl(F_DUPFD)` is unchanged because its
caller supplies an explicit minimum. Augment the production FD host model
to test closed standard descriptors and two-FD capacity, and require
three native-QEMU gates from original BusyBox: background sleep/wait, FD0
close-reopen, and background pipeline output. M67 original pgrep/pkill and
all existing CI acceptance gates must remain green.

## M68 additional real blocker: rt_sigsuspend / caught SIGCHLD

The first exact-head QEMU rerun proved the earlier `/dev/null` error was
gone and the background child **did run and exit 0**, but BusyBox ash
then blocked in `rt_sigsuspend`, previously unimplemented (raw Linux
x86 syscall 130, asm-generic RV64/ARM64 syscall 133). It was not a pipe
or /dev/null error: the actual serial trace showed
`[MISS] syscall nr=130` in the waiting shell.

Add common `NAS_SC_rt_sigsuspend` decoding on all three ABIs and enter the
existing Task wait owner with an atomic temporary signal mask. The actual
caught pending SIGCHLD alone interrupts the syscall, wakes the task with
`-EINTR`, restores its saved mask and installs the real ISA signal
handler on user return. No synthetic successful signal syscall, no
scheduler busy loop, and no alternate task registry. The real unmodified
ash `&`/wait regression is an exact-head merge gate on all three ISAs.
