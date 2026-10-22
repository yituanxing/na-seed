# M25 — POSIX controlling-terminal background job control

M23 handles terminal Ctrl+C and Ctrl+Z delivered to the foreground process
group. M25 closes the inverse gap: a background process group must not
silently read the controlling terminal or write to it with termios TOSTOP.

Linux/POSIX terminal behavior:
- A background `read(/dev/tty)` receives SIGTTIN; default action STOPs
  the whole process group and is visible via `waitpid(WUNTRACED)`.
- If the background reader ignores or blocks SIGTTIN, or the process group
  is orphaned, `read` reports Linux EIO, not fake bytes or a hung syscall.
- A background `write(/dev/tty)` when TOSTOP is set receives SIGTTOU and
  STOPs. With default TOSTOP clear, background write is permitted.
- Background TIOCSPGRP is also subject to SIGTTOU unless ignored/blocked,
  allowing a shell to restore its own foreground group by SIG_IGN.
- On SIGCONT, interrupted syscall returns EINTR at its saved user boundary;
  a genuine userspace caller can retry the syscall once it is foreground.
- Explicit `FD_TTY` is subject to job policy. Startup FD_UART remains a raw
  diagnostic console so immutable BusyBox noninteractive tests do not change.
- Custom caught SIGTTIN/SIGTTOU handlers require user rt_sigframe and are
  explicitly rejected. This does not claim complete POSIX signal handling.

One unchanged normal C program is compiled by native GNU/Linux compilers
for x86-64, RISC-V64, ARM64. In three full QEMU kernels, it:
1. Forks a background job, validates waitpid(WUNTRACED) returns SIGTTIN.
2. Gives foreground TTY to that PGID, SIGCONT, then reads REAL serial bytes
   injected only after foreground ownership is confirmed by the parent.
3. Tests background TOSTOP write SIGTTOU, restores foreground and SIGCONT.
4. Tests a background reader ignoring SIGTTIN and returning exactly EIO.

Existing three-ISA original BusyBox Debian/Alpine, 14/14 controlled source
programs, true interactive ash, poll/ppoll and prior terminal signal jobs
remain mandatory. No new hardware-specific copies of TTY or scheduler code.
