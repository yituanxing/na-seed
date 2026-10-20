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
