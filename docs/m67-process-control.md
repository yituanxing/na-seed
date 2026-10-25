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
