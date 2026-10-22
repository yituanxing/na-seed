# M17 — real console TTY session, foreground process group and COM1 input

Real Linux `tcgetattr/tcsetattr`, `tcgetpgrp/tcsetpgrp`, `/dev/tty`
and `TIOCGSID` under the same 3 native ABI compilers/QEMU kernels.

- Implement Linux TCGETS 36-byte kernel wire ABI rather than glibc's
  larger user `struct termios`. Select TIOCSCTTY controlling session only
  with session-leader permission. Validate TIOCSPGRP belongs to that SID.
- Expose `/dev/console` and `/dev/ttyS0` as actual console file-descriptors;
  `/dev/tty` is available only to a controlling-terminal session.
  Non-tty descriptors continue returning ENOTTY.
- One generic driver module owns terminal session, foreground group,
  canonical line editing, ICRNL, ECHO and per-line unread bytes;
  architecture transport provides only UART/RX/TX mechanism.
- Real x86 COM1 UART RX is physically polled. Only an explicit
  TIOCSCTTY call enables blocking RX, preserving the existing original
  noninteractive BusyBox programs that expect no console input.
  A real QEMU stdio serial runner injects the 6 input bytes only after
  the C guest announces readiness. The guest validates `hello\\n`.
- RV64 SBI and ARM64 semihost consoles are OUTPUT ONLY. No invented RX;
  interactive reads explicitly return EOPNOTSUPP on those hardware paths.
- This milestone does NOT yet implement terminal-generated SIGINT or
  SIGTSTP, background tty read SIGTTIN, asynchronous scheduler-driven UART
  wakeups, VTIME, or PTY. Control-C produces EOPNOTSUPP, not fake success.
  Full portable interactive BusyBox ash remains a future milestone.

Acceptance: `tests/m17-tty.c` runs under an actual Linux host PTY
(`script`), QEMU-user under the same host PTY, and the three full
NaSeed kernels. It verifies opening controlling /dev/tty, termios
roundtrip, foreground child group switch/restore, real pipe/parent
handshake, and signaled child wait. `tests/m17-input.c` runs as
actual x86 PID1 via CPIO ELF and consumes live QEMU COM1 input.
The immutable original Debian and Alpine binary matrices remain intact.
