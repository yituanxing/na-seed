# NaSeed

**An operating system grown by real software.**

NaSeed is a small, independently developed kernel. We start with a minimal bootable x86-64 system and expand it in response to observable behavior of **real, unmodified userspace programs** (starting with statically linked GNU Bash). Actual device drivers are a core long-term goal, not an afterthought.

## Development contract

1. Boot a real kernel under Limine and QEMU, with serial diagnostics.
2. Add an executable userspace ELF and validate the Linux x86-64 syscall ABI.
3. Run a fixed, statically linked GNU Bash and fix **classes of observed failures**, not isolated test cases.
4. Keep integration tests and prior milestones green; distinguish missing syscalls, incorrect syscall semantics, and CPU exceptions.
5. Add genuine hardware/virtual-device driver support as real applications demand it.

Track milestones M0 (boot + memory + serial), M1 (ELF + ring 3 + write/exit), M2 (Bash -c), M3 (interactive Bash), and later external commands, pipes and drivers.

This project is independent of [NaOS](https://github.com/aether-os-studio/na-kernel), which is a reference, not a codebase to trim down.

## Run multiple unmodified BusyBox applets (M4.6)

NaSeed follows NaOS's Limine `executable_cmdline_request` approach.
The optional Limine entry `cmdline: naseed.argv=/bin/busybox uname -s`
sets **user-space argv** without recompiling any program or modifying kernel
source for each new test. Single/double quotes and backslash escaping group
arguments; this is not a shell. A malformed nonempty cmdline is rejected,
never silently accepted or ignored. With no cmdline, all existing M0-M4
regressions retain their original argv and strict output checks.

`make alpine-argv-probe` checks the **same official Alpine BusyBox binary**
against `uname -s`, `pwd`, and `id -u` using separate Limine boot
configurations. The host independently validates exact stdout, zero exit,
and QEMU debug-exit code. Passing this test is not a claim of general VFS,
process creation, or terminal support. New applets that require those
capabilities should report blockers, not receive fake syscall success.
