# M54 — Native, integrated development lifecycle on NaSeed

## Goal
Use one unchanged Alpine v3.23 signed rootfs with real Vim, Git, GNU Make,
GCC, original Python 3 and BusyBox. Do not accept five disjoint tests as
evidence of a working interactive development environment. The same guest
session must share real tmpfs, process scheduler, POSIX file operations,
executable ELF loader, clocks, stdlib and Git object storage.

## Native acceptance — one real kernel boot per ISA
1. Copy real project source/headers/Makefile to tmpfs; git init/add/commit.
2. Original Vim Ex editor opens the *same* C source in the Git worktree,
   changes `return 1;` to `return 7;`, writes; original Git diff sees it.
3. GNU Make -j2 invokes two independent real Alpine GCC compilations and
   performs a genuine musl static link, running the compiled binary.
4. Original CPython subprocess executes the *guest-built* binary, checks
   stdout/exit/ELF, hashes bytes, records a transaction in real SQLite,
   and cross-checks Git's dirty worktree.
5. Git stages/commits the Vim change; status clean, history verified.
6. A second Vim edit to `return 9;` triggers true incremental Make,
   a second guest GCC/link/run, Python subprocess/SQLite second record,
   and a final Git commit, diff/history/status verification.

Program names and contents are irrelevant to the kernel: no application
specific syscalls or host-emulated acceptance. No network, persistent disk,
desktop, SMP or parallel kernel stacks added.

The physical UART Vim editing was independently fully verified as M53 on
all three ISAs, and remains a mandatory unchanged regression. The *M54
composition* uses original Vim's real Ex editor over writable tmpfs, not
a claim of an interactive UART session.

CI: append one workload/ISA to existing 12 grouped native jobs (M38-M54
now 16 workload definitions). Keep old original 45 real QEMU sessions
plus 3 integrated project builds; 48 checks overall, and all six normal
CI workflows remain required. Dedicated M54 diagnostic is manual-only.
Retain fast fail-closed stage markers and QEMU serial failure evidence.
