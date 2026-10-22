# M22: shared poll/ppoll event readiness for TTY and anonymous pipes

M21 made terminal reads blockable; upstream software also expects poll(2)
to report actual terminal readiness and wait on multiple file sources.

One native Linux C PID1 uses poll(POLLIN) on controlling /dev/tty, then
forks a child blocked on poll(tty,-1). Parent advances through a genuine
pipe handshake and prints its progress marker. Only then the host sends
real physical "poll\r" to QEMU COM1, RV64 OpenSBI or ARM64 PL011. The
child wakes when the N_TTY line discipline has a COMPLETE canonical
line, reads "poll\n", and parent reaps it. A second child blocks on
poll(pipe,-1) until its parent writes a REAL pipe byte. Invalid FDs
yield POLLNVAL; nonblocking poll(timeout=0) correctly returns 0.

Linux ABI has different syscall identities: x86-64 poll(7), generic64
ppoll(73). Both delegate to the same kernel readiness checker; ppoll's
NULL timeout means indefinite wait and zero timespec is nonblocking.
Finite timeouts or temporary sigmask exchange require real clock and
signal-frame mechanisms; they explicitly return EOPNOTSUPP rather than
pretend a timeout elapsed.

When a task sleeps in poll, a KERNEL-OWNED snapshot stores pipe endpoint
slot bitmaps and whether TTY RX is requested. Scheduler must NEVER read
foreign userspace pollfd pointers while deciding whether to wake; it
replays the syscall only after restoring owning VM/CPU/TLS/FPU state.
If no real wakeable FD is supplied to an indefinite wait, report
EOPNOTSUPP, not false completion. M21 physical UART idle still POLLS
hardware; no fake interrupt/low-power capability is claimed.

Full unmodified Debian/Alpine distro matrices, all 13 source BusyBox
cases, existing eight-command real ash sessions and M21 fork/TTY test
remain mandatory for all three ISAs.
