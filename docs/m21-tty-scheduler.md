# M21 — real TTY scheduler parking and physical input wake

One ordinary Linux C PID1 starts a child that signals readiness over a real
pipe, then blocks reading its actual controlling /dev/tty. The parent must
run and print the progress marker while its sibling is blocked. The host
sends wake+CR only after seeing that marker through QEMU COM1, OpenSBI
getchar, or AArch64 PL011. Child must read wake+newline, exit zero, and
be reaped by real waitpid. Same source builds for all three Linux ABIs.

Shared TTY core pumps up to 32 hardware bytes at a time, then feeds the
M20 line discipline. It has no user buffer and does not switch the VM.
Blocking tty read returns EAGAIN when empty; the syscall dispatcher
parks only real TTY or attached UART, only for blocking descriptors.
The scheduler replays the read with correct MMU/TLS/FPU after real line
readiness. Nonblocking reads never park. Unsupported VTIME and ISIG
semantics still fail explicitly, not as a false wake.

A single last task blocked on input is not a deadlock: scheduler idle
scans real hardware until input arrives. This is still a polled idle
loop, NOT an asynchronous UART IRQ or low-power WFI implementation.
RISC-V SBI legacy getchar does not provide a portable RX interrupt.
Full UART IRQ/event scheduling and terminal-generated job-control signals
remain later capabilities.

All Debian/Alpine native binary regressions, 13 source BusyBox workload
oracles and interactive eight-command ash tests remain mandatory; the
M21 child-parent native test is added to the same CI, not substituted.
