# First game played in NaSeed: original 2048.c

We selected a real game because we want to **play** something on our own
operating system, not because a specific syscall was next on a checklist.

Upstream source: `mevdschee/2048.c`, commit
`90343eb209e84fa9687d596f5d5eecfdf7d4df51`, blob
`a1673a2cfe86de6bc50a6c9db0e5bd125395ab74` (MIT licence).
Build from unchanged source using the real native Linux GCC and static
glibc. If fetched bytes differ from the immutable Git object hash, the
build fails. No bundled imitation of the board logic.

Acceptance has **three distinct** steps: (1) run the original game's own
13-unit-test suite; (2) run the original ELF under a Linux PTY, make four
actual keyboard moves, observe game board refresh and quit confirmation;
(3) boot the actual NaSeed x86-64 kernel in UEFI system-QEMU with a
BusyBox-free CPIO, exec the *identical original ELF* as a genuine terminal
process, move tiles via physical COM1 bytes, then quit with q/y. A guest
exit=0 **alone** does not pass; visible game board, move refresh, quit
confirmation and the real userspace exit marker are all mandatory.

The PID1 helper creates the controlling TTY and performs execve. There
are no game-only kernel shims or patched userspace. If the game demands
additional general timer, tty, signal or process behavior, fix the common
subsystem and keep its unrelated BusyBox/nano regressions intact.

Initial acceptance is x86-64 only. We can play on one architecture
before requiring all other ISAs. Running GNU nano, TinyCC, Lua and a game
on one kernel demonstrates breadth rather than "fully implementing"
one application.
